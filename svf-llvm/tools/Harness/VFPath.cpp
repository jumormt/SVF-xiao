//===- VFPath.cpp -- value-flow path queries (vfpath / reachable) --------===//
//
// Third translation unit of QueryEngine: SVFG source/sink anchor resolution
// and the BFS path search behind vfpath and reachable. Split out of
// Queries.cpp, which would otherwise exceed ~800 lines (Task 5.1).
//
// Search strategy (documented honestly in Schema.cpp): ONE multi-source BFS
// over SVFG out-edges recording a parent tree. Each distinct sink SVFG node
// therefore yields at most one witness path (shortest by edge count, ties by
// discovery order), so vfpath returns at most min(k, #reachable sink nodes)
// paths even when longer or differently-routed paths exist. A returned path
// is a MAY value flow (Andersen + memory SSA over-approximation), not a
// proof of a feasible execution.
//
//===----------------------------------------------------------------------===//
#include "QueryEngine.h"
#include "AnchorDoc.h"
#include "Evidence.h"
#include "Graphs/ICFG.h"
#include "Graphs/SVFG.h"
#include "Graphs/SVFGEdge.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFVariables.h"
#include "Util/GeneralType.h"
#include "Util/SVFUtil.h"
#include <algorithm>
#include <deque>
#include <stdexcept>

using namespace SVF;
using json = nlohmann::json;

namespace
{
/// Default BFS budget: max SVFG nodes dequeued before giving up.
constexpr size_t kDefaultMaxVisited = 100000;
/// vfpath k bounds.
constexpr int kMaxPaths = 10;
/// reachable: max sink anchors per call.
constexpr size_t kSinkCap = 20;
/// Per-path step cap: keep first half + last half, insert elision marker.
constexpr size_t kPathStepCap = 500;

/// Concrete SVFGEdge class name for a path step's `edge` label. isa<> on the
/// CONCRETE classes only — never derive labels from toString() prefixes:
/// abstract bases print misleading names (DirectSVFGEdge prints
/// "DirectVFGEdge"), see the Schema.cpp header warning.
const char* edgeKindName(const VFGEdge* e)
{
    if (SVFUtil::isa<CallDirSVFGEdge>(e))
        return "CallDirSVFGEdge";
    if (SVFUtil::isa<RetDirSVFGEdge>(e))
        return "RetDirSVFGEdge";
    if (SVFUtil::isa<IntraDirSVFGEdge>(e))
        return "IntraDirSVFGEdge";
    if (SVFUtil::isa<CallIndSVFGEdge>(e))
        return "CallIndSVFGEdge";
    if (SVFUtil::isa<RetIndSVFGEdge>(e))
        return "RetIndSVFGEdge";
    if (SVFUtil::isa<ThreadMHPIndSVFGEdge>(e))
        return "ThreadMHPIndSVFGEdge";
    if (SVFUtil::isa<IntraIndSVFGEdge>(e))
        return "IntraIndSVFGEdge";
    return "VFGEdge"; // defensive; every SVFG edge is one of the above
}

/// CallICFGNode of a Call*/Ret* SVFG edge (null for intra/thread edges).
/// The four interprocedural edge classes each carry a CallSiteID;
/// svfg->getCallSite(id) maps it back to the CallICFGNode (VFG.h delegates
/// to CallGraph::getCallSite).
const CallICFGNode* edgeCallSite(const VFGEdge* e, const SVFG* svfg)
{
    if (const auto* cd = SVFUtil::dyn_cast<CallDirSVFGEdge>(e))
        return svfg->getCallSite(cd->getCallSiteId());
    if (const auto* rd = SVFUtil::dyn_cast<RetDirSVFGEdge>(e))
        return svfg->getCallSite(rd->getCallSiteId());
    if (const auto* ci = SVFUtil::dyn_cast<CallIndSVFGEdge>(e))
        return svfg->getCallSite(ci->getCallSiteId());
    if (const auto* ri = SVFUtil::dyn_cast<RetIndSVFGEdge>(e))
        return svfg->getCallSite(ri->getCallSiteId());
    return nullptr;
}

/// State of one multi-source BFS over SVFG out-edges.
struct Search
{
    /// node id -> (predecessor node, edge taken INTO the node); sources map
    /// to {nullptr, nullptr}. Doubles as the visited/discovered set.
    SVF::Map<NodeID, std::pair<const VFGNode*, const VFGEdge*>> parent;
    size_t visited = 0;     ///< nodes dequeued (charged against the budget)
    bool truncated = false; ///< budget exhausted with frontier left
};

/// Multi-source BFS. onHit(node) is invoked exactly once per discovered
/// target node (including a source that is itself a target); returning true
/// stops the search early (enough hits collected).
template <typename OnHit>
Search bfs(const std::vector<const VFGNode*>& sources,
           const SVF::Set<NodeID>& targets, size_t budget, OnHit onHit)
{
    Search s;
    std::deque<const VFGNode*> queue;
    bool stop = false;
    auto discover = [&](const VFGNode* n, const VFGNode* prev,
                        const VFGEdge* via)
    {
        if (!s.parent.emplace(n->getId(), std::make_pair(prev, via)).second)
            return; // already discovered
        if (targets.count(n->getId()) && onHit(n))
        {
            stop = true;
            return;
        }
        queue.push_back(n);
    };
    for (const VFGNode* src : sources)
    {
        discover(src, nullptr, nullptr);
        if (stop)
            return s;
    }
    while (!queue.empty() && !stop)
    {
        if (s.visited >= budget)
        {
            s.truncated = true;
            break;
        }
        const VFGNode* n = queue.front();
        queue.pop_front();
        ++s.visited;
        for (const VFGEdge* e : n->getOutEdges())
        {
            discover(e->getDstNode(), n, e);
            if (stop)
                break;
        }
    }
    return s;
}

/// Apply middle elision to a steps array: keep the first `cap/2` and last
/// `cap/2` steps, insert a {"elided_steps": N} marker between them.
/// Returns the elided array and sets *truncated = true.
/// If steps.size() <= cap, returns the array unchanged and leaves *truncated.
json elideSteps(json steps, size_t cap, bool* truncated)
{
    if (steps.size() <= cap)
        return steps;
    const size_t half = cap / 2;                     // 250 for cap=500
    const size_t elided = steps.size() - 2 * half;
    json out = json::array();
    for (size_t i = 0; i < half; ++i)
        out.push_back(std::move(steps[i]));
    out.push_back(json{{"elided_steps", elided}});
    for (size_t i = steps.size() - half; i < steps.size(); ++i)
        out.push_back(std::move(steps[i]));
    *truncated = true;
    return out;
}

/// Reconstruct the witness path ending at `sink` from the BFS parent tree:
/// {steps: [{node, edge: null}, {node, edge, callsite?}, ...], length}.
/// The first step's edge is null (it is the source); Call*/Ret* steps carry
/// the CallICFGNode evidence of the crossed callsite.
/// `maxSteps` caps the emitted steps via middle elision (cap/2 head + tail
/// with an {"elided_steps": N} marker); absent/0 means use kPathStepCap.
json buildPath(const Search& s, const VFGNode* sink, const SVFG* svfg,
               size_t maxSteps = 0)
{
    if (maxSteps == 0)
        maxSteps = kPathStepCap;
    std::vector<std::pair<const VFGNode*, const VFGEdge*>> rev;
    for (const VFGNode* cur = sink; cur;)
    {
        const auto& p = s.parent.at(cur->getId());
        rev.emplace_back(cur, p.second);
        cur = p.first;
    }
    json steps = json::array();
    for (auto it = rev.rbegin(); it != rev.rend(); ++it)
    {
        json step{{"node", evidence::node(it->first)},
                  {"edge",
                   it->second ? json(edgeKindName(it->second)) : json()}};
        if (it->second)
            if (const CallICFGNode* cs = edgeCallSite(it->second, svfg))
                step["callsite"] = evidence::node(cs);
        steps.push_back(std::move(step));
    }
    bool truncated = false;
    steps = elideSteps(std::move(steps), maxSteps, &truncated);
    json path{{"steps", std::move(steps)}, {"length", rev.size()}};
    if (truncated)
        path["steps_truncated"] = true;
    return path;
}

size_t budgetParam(const json& params)
{
    if (!params.contains("max_visited"))
        return kDefaultMaxVisited;
    if (!params["max_visited"].is_number_integer() ||
        params["max_visited"].get<long long>() < 1)
        throw std::runtime_error("max_visited must be a positive integer");
    return params["max_visited"].get<size_t>();
}

/// Optional max_steps param: range [10, 500], default kPathStepCap.
size_t maxStepsParam(const json& params)
{
    if (!params.contains("max_steps"))
        return kPathStepCap;
    if (!params["max_steps"].is_number_integer())
        throw std::runtime_error("max_steps must be an integer");
    const long long v = params["max_steps"].get<long long>();
    if (v < 10 || v > static_cast<long long>(kPathStepCap))
        throw std::runtime_error(
            "max_steps must be in [10, " + std::to_string(kPathStepCap) + "]");
    return static_cast<size_t>(v);
}

const json& requiredAnchor(const json& params, const char* key)
{
    if (!params.contains(key))
        throw std::runtime_error(std::string("missing required param: ") +
                                 key + "; " + kAnchorForms);
    return params[key];
}
} // namespace

std::vector<const VFGNode*>
QueryEngine::resolveSourceNodes(const json& spec) const
{
    const std::vector<const SVFVar*> vars = resolveVars(spec);
    std::vector<const VFGNode*> out;
    std::set<NodeID> seen;
    for (const SVFVar* v : vars)
    {
        // hasDefSVFGNode guard (svf-ex.cpp traverseOnVFG pattern): the def
        // map covers top-level ValVars only, and address-taken/never-defined
        // vars have no def node in the SVFG.
        const ValVar* vv = SVFUtil::dyn_cast<ValVar>(v);
        if (!vv || !svfg->hasDefSVFGNode(vv))
            continue;
        const VFGNode* def = svfg->getDefSVFGNode(vv);
        if (seen.insert(def->getId()).second)
            out.push_back(def);
    }
    if (out.empty())
        throw std::runtime_error(
            "source anchor resolved to " + std::to_string(vars.size()) +
            " var(s), but none has a value-flow definition node (only "
            "top-level values defined in the SVFG can act as sources). " +
            kAnchorForms);
    std::sort(out.begin(), out.end(),
              [](const VFGNode* a, const VFGNode* b)
    {
        return a->getId() < b->getId();
    });
    return out;
}

std::vector<const VFGNode*>
QueryEngine::resolveSinkNodes(const json& spec) const
{
    if (!spec.is_object())
        throw std::runtime_error(
            std::string("sink anchor must be an object; ") + kAnchorForms);
    std::vector<const VFGNode*> out;
    std::set<NodeID> seen;
    auto add = [&](const VFGNode* n)
    {
        if (n && seen.insert(n->getId()).second)
            out.push_back(n);
    };

    if (spec.contains("file") || spec.contains("line"))
    {
        // Bare {file, line}: every SVFG node attached to an ICFG node at
        // that source line — i.e. any value-flow USE or DEF there can end a
        // path. Optional "name" filters by the node's top-level value name.
        if (!spec.contains("file") || !spec["file"].is_string() ||
            !spec.contains("line") || !spec["line"].is_number_integer())
            throw std::runtime_error(
                std::string("file:line anchor needs string \"file\" and "
                            "integer \"line\"; ") + kAnchorForms);
        const std::string file = spec["file"].get<std::string>();
        const int line = spec["line"].get<int>();
        const std::string name = spec.value("name", "");
        std::set<int> vfgLines; // lines with value-flow nodes — hint material
        size_t unfiltered = 0;  // nodes at the line before the name filter
        for (const auto& it : *pag->getICFG())
        {
            const ICFGNode* node = it.second;
            const json l = evidence::loc(node->getSourceLoc());
            if (!evidence::fileMatches(l["file"].get_ref<const std::string&>(),
                                       file))
                continue;
            const int nodeLine = l["line"].get<int>();
            if (!node->getVFGNodes().empty() && nodeLine > 0)
                vfgLines.insert(nodeLine);
            if (nodeLine != line)
                continue;
            for (const VFGNode* vn : node->getVFGNodes())
            {
                ++unfiltered;
                if (!name.empty())
                {
                    const SVFVar* val = vn->getValue();
                    if (!val ||
                        val->getValueName().find(name) == std::string::npos)
                        continue;
                }
                add(vn);
            }
        }
        if (out.empty())
        {
            if (!name.empty() && unfiltered > 0)
                throw std::runtime_error(
                    std::to_string(unfiltered) + " value-flow node(s) at " +
                    file + ":" + std::to_string(line) + " have empty/other "
                    "value names (none matches '" + name + "'); the IR may "
                    "have been built without -fno-discard-value-names — "
                    "drop the name filter. " + kAnchorForms);
            std::string msg = "no value-flow nodes at file " + file +
                              " line " + std::to_string(line);
            if (!vfgLines.empty())
            {
                std::vector<int> lines(vfgLines.begin(), vfgLines.end());
                std::sort(lines.begin(), lines.end(), [&](int a, int b)
                {
                    const int da = std::abs(a - line), db = std::abs(b - line);
                    if (da != db)
                        return da < db;
                    return a < b;
                });
                if (lines.size() > 5)
                    lines.resize(5);
                std::sort(lines.begin(), lines.end());
                msg += "; nearest lines with value-flow nodes:";
                for (int ln : lines)
                    msg += " " + std::to_string(ln);
            }
            throw std::runtime_error(msg + ". " + kAnchorForms);
        }
    }
    else
    {
        // Var-form sink ({func, ret/arg}): the var's def node plus every
        // SVFG node whose top-level value IS that var (its loads/stores/
        // copies). Kept deliberately simple in v0; documented in schema.
        const std::vector<const SVFVar*> vars = resolveVars(spec);
        std::set<NodeID> varIds;
        for (const SVFVar* v : vars)
        {
            varIds.insert(v->getId());
            const ValVar* vv = SVFUtil::dyn_cast<ValVar>(v);
            if (vv && svfg->hasDefSVFGNode(vv))
                add(svfg->getDefSVFGNode(vv));
        }
        for (const auto& it : *svfg)
        {
            const VFGNode* n = it.second;
            const SVFVar* val = n->getValue();
            if (val && varIds.count(val->getId()))
                add(n);
        }
        if (out.empty())
            throw std::runtime_error(
                "sink anchor resolved to " + std::to_string(vars.size()) +
                " var(s), but none corresponds to any value-flow node. " +
                kAnchorForms);
    }
    std::sort(out.begin(), out.end(),
              [](const VFGNode* a, const VFGNode* b)
    {
        return a->getId() < b->getId();
    });
    return out;
}

json QueryEngine::vfpath(const json& params) const
{
    const json& srcSpec = requiredAnchor(params, "source");
    const json& sinkSpec = requiredAnchor(params, "sink");
    int k = 1;
    if (params.contains("k"))
    {
        if (!params["k"].is_number_integer() ||
            params["k"].get<int>() < 1 || params["k"].get<int>() > kMaxPaths)
            throw std::runtime_error("k must be an integer in [1, 10]");
        k = params["k"].get<int>();
    }
    const size_t budget = budgetParam(params);
    const size_t maxSteps = maxStepsParam(params);
    const std::vector<const VFGNode*> sources = resolveSourceNodes(srcSpec);
    const std::vector<const VFGNode*> sinks = resolveSinkNodes(sinkSpec);
    SVF::Set<NodeID> targets;
    for (const VFGNode* n : sinks)
        targets.insert(n->getId());

    std::vector<const VFGNode*> hits;
    const Search s = bfs(sources, targets, budget, [&](const VFGNode* n)
    {
        hits.push_back(n);
        return hits.size() >= static_cast<size_t>(k);
    });
    json paths = json::array();
    for (const VFGNode* h : hits)
        paths.push_back(buildPath(s, h, svfg, maxSteps));
    return json{{"paths", std::move(paths)},
                {"sources", sources.size()},
                {"sinks", sinks.size()},
                {"visited", s.visited},
                {"truncated", s.truncated}};
}

json QueryEngine::reachable(const json& params) const
{
    const json& srcSpec = requiredAnchor(params, "source");
    if (!params.contains("sinks") || !params["sinks"].is_array() ||
        params["sinks"].empty())
        throw std::runtime_error(
            std::string("missing required param: sinks (non-empty array of "
                        "sink anchors); ") + kAnchorForms);
    const json& sinkSpecs = params["sinks"];
    if (sinkSpecs.size() > kSinkCap)
        throw std::runtime_error(
            "too many sinks: " + std::to_string(sinkSpecs.size()) +
            " (cap is " + std::to_string(kSinkCap) +
            " per call; split into batches)");
    const size_t budget = budgetParam(params);
    const size_t maxSteps = maxStepsParam(params);
    // SOURCE failure is fatal (invalidates the whole call).
    const std::vector<const VFGNode*> sources = resolveSourceNodes(srcSpec);

    // Sink specs may share SVFG nodes; map node -> spec indexes so one BFS
    // serves all sinks and the first hit per spec becomes its witness.
    // Per-sink resolution failures yield a result row with reachable=false
    // and an "error" field rather than aborting the whole call.
    SVF::Map<NodeID, std::vector<size_t>> nodeToSinks;
    SVF::Set<NodeID> targets;
    std::vector<std::string> sinkErrors(sinkSpecs.size());
    size_t validSinks = 0;
    for (size_t i = 0; i < sinkSpecs.size(); ++i)
    {
        try
        {
            for (const VFGNode* n : resolveSinkNodes(sinkSpecs[i]))
            {
                nodeToSinks[n->getId()].push_back(i);
                targets.insert(n->getId());
            }
            ++validSinks;
        }
        catch (const std::exception& e)
        {
            sinkErrors[i] = e.what();
        }
    }

    std::vector<const VFGNode*> witness(sinkSpecs.size(), nullptr);
    size_t remaining = validSinks;
    Search s;
    if (!targets.empty())
    {
        s = bfs(sources, targets, budget, [&](const VFGNode* n)
        {
            for (size_t i : nodeToSinks[n->getId()])
            {
                if (!witness[i])
                {
                    witness[i] = n;
                    if (--remaining == 0)
                        return true;
                }
            }
            return false;
        });
    }
    json results = json::array();
    for (size_t i = 0; i < sinkSpecs.size(); ++i)
    {
        if (!sinkErrors[i].empty())
        {
            results.push_back(json{{"sink", sinkSpecs[i]},
                                   {"reachable", false},
                                   {"error", sinkErrors[i]}});
            continue;
        }
        json r{{"sink", sinkSpecs[i]},
               {"reachable", witness[i] != nullptr}};
        if (witness[i])
            r["first_path"] = buildPath(s, witness[i], svfg, maxSteps);
        results.push_back(std::move(r));
    }
    return json{{"results", std::move(results)},
                {"sources", sources.size()},
                {"visited", s.visited},
                {"truncated", s.truncated}};
}
