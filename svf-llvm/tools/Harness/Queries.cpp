//===- Queries.cpp -- QueryEngine query method bodies --------------------===//
//
// Second translation unit of QueryEngine (same class): the per-method query
// bodies live here, while bootstrap, dispatch and the function-lookup helpers
// stay in QueryEngine.cpp. Split when Task 4.3 roughly doubled the file.
//
//===----------------------------------------------------------------------===//
#include "QueryEngine.h"
#include "Evidence.h"
#include "Schema.h"
#include "Graphs/CallGraph.h"
#include "Graphs/ICFG.h"
#include "Graphs/SVFG.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFStatements.h"
#include "SVFIR/SVFVariables.h"
#include "Util/SVFUtil.h"
#include "WPA/Andersen.h"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>

using namespace SVF;
using json = nlohmann::json;

namespace
{
/// Cap on resolved vars per defuse/pts/aliases query (mirrors the other
/// methods' 200-row cap).
constexpr size_t kVarCap = 200;
/// Per-var cap on reported aliases.
constexpr size_t kAliasCap = 50;
/// cfg caps: nodes and edges are truncated separately.
constexpr size_t kCfgNodeCap = 500;
constexpr size_t kCfgEdgeCap = 1000;

const char* kAnchorForms =
    "accepted var anchor forms: "
    "{\"file\": \"demo.c\", \"line\": 8} (all vars defined at that line; add "
    "\"name\": \"b\" to filter by value-name substring), "
    "{\"func\": \"malloc\", \"ret\": true} (return value at every callsite "
    "of func), "
    "{\"func\": \"memcpy\", \"arg\": 0} (actual argument at every callsite "
    "of func)";

/// True when ``locFile`` (full path from debug info) refers to the
/// user-given ``wanted`` file: equal, or path-suffix at a '/' boundary.
bool fileMatches(const std::string& locFile, const std::string& wanted)
{
    if (locFile.empty() || wanted.empty() || locFile.size() < wanted.size())
        return false;
    if (locFile.compare(locFile.size() - wanted.size(), wanted.size(),
                        wanted) != 0)
        return false;
    return locFile.size() == wanted.size() ||
           locFile[locFile.size() - wanted.size() - 1] == '/';
}

/// SVFStmt kind name for defuse output, from the PEDGEK enum. A switch (not
/// a toString() prefix): SVFStmt dumps start with "SVFStmt: [" rather than
/// the subclass name, so the Evidence.cpp prefix trick does not apply.
const char* stmtKindName(const SVFStmt* s)
{
    switch (s->getEdgeKind())
    {
    case SVFStmt::Addr:
        return "Addr";
    case SVFStmt::Copy:
        return "Copy";
    case SVFStmt::Store:
        return "Store";
    case SVFStmt::Load:
        return "Load";
    case SVFStmt::Call:
        return "Call";
    case SVFStmt::Ret:
        return "Ret";
    case SVFStmt::Gep:
        return "Gep";
    case SVFStmt::Phi:
        return "Phi";
    case SVFStmt::Select:
        return "Select";
    case SVFStmt::Cmp:
        return "Cmp";
    case SVFStmt::BinaryOp:
        return "BinaryOp";
    case SVFStmt::UnaryOp:
        return "UnaryOp";
    case SVFStmt::Branch:
        return "Branch";
    case SVFStmt::ThreadFork:
        return "ThreadFork";
    case SVFStmt::ThreadJoin:
        return "ThreadJoin";
    default:
        return "SVFStmt";
    }
}

/// {stmt: <kind>, at: <evidence of the stmt's ICFG node>} for defuse rows.
json stmtRecord(const SVFStmt* s)
{
    const ICFGNode* at = s->getICFGNode();
    return json{{"stmt", stmtKindName(s)},
                {"at", at ? evidence::node(at) : json()}};
}

/// Sorted-by-ICFG-node-id copy of a var's stmt edge set, for stable output.
std::vector<const SVFStmt*> sortedStmts(const SVFStmt::SVFStmtSetTy& edges)
{
    std::vector<const SVFStmt*> v(edges.begin(), edges.end());
    std::sort(v.begin(), v.end(), [](const SVFStmt* a, const SVFStmt* b)
    {
        const NodeID ai = a->getICFGNode() ? a->getICFGNode()->getId() : 0;
        const NodeID bi = b->getICFGNode() ? b->getICFGNode()->getId() : 0;
        if (ai != bi)
            return ai < bi;
        return a->getEdgeID() < b->getEdgeID();
    });
    return v;
}

const std::string& requiredString(const json& params, const char* key)
{
    if (!params.contains(key) || !params[key].is_string())
        throw std::runtime_error(std::string("missing required string param: ") +
                                 key);
    return params[key].get_ref<const std::string&>();
}

const json& requiredVarAnchor(const json& params)
{
    if (!params.contains("var"))
        throw std::runtime_error(std::string("missing required param: var; ") +
                                 kAnchorForms);
    return params["var"];
}
} // namespace

json QueryEngine::functions(const json& params) const
{
    std::string pattern = params.value("pattern", "");
    std::regex re;
    bool filter = !pattern.empty();
    if (filter)
    {
        try
        {
            re = std::regex(pattern, std::regex::ECMAScript);
        }
        catch (const std::regex_error& e)
        {
            throw std::runtime_error("invalid pattern: " + pattern + " (" +
                                     e.what() + ")");
        }
    }
    std::vector<const FunObjVar*> funs;
    for (const auto& it : *callgraph)
    {
        const FunObjVar* fun = it.second->getFunction();
        if (!fun)
            continue;
        if (filter && !std::regex_search(fun->getName(), re))
            continue;
        funs.push_back(fun);
    }
    // Sort by (name, node-id) so same-named statics from different TUs order
    // deterministically across runs.
    std::sort(funs.begin(), funs.end(),
              [&](const FunObjVar* a, const FunObjVar* b)
    {
        if (a->getName() != b->getName())
            return a->getName() < b->getName();
        return a->getId() < b->getId();
    });
    constexpr size_t kCap = 200;
    size_t total = funs.size(); // pre-cap match count
    bool truncated = funs.size() > kCap;
    if (truncated)
        funs.resize(kCap);
    json out = json::array();
    for (const FunObjVar* fun : funs)
    {
        out.push_back({{"name", fun->getName()},
                       {"loc", evidence::loc(fun->getSourceLoc())},
                       {"is_decl", fun->isDeclaration()},
                       {"num_args", fun->arg_size()}});
    }
    return json{{"functions", std::move(out)}, {"truncated", truncated},
                {"total", total}};
}

json QueryEngine::callEdges(const json& params, bool incoming) const
{
    const std::string name = requiredString(params, "func");

    // Use findFunctions (merge semantics): collect results from ALL nodes
    // that have this name (handles same-named statics across translation
    // units). Never raises an ambiguity error — callers/callees always merge.
    const std::vector<const CallGraphNode*> nodes = findFunctions(name);
    if (nodes.empty())
    {
        // No matches: delegate to findFunction to produce the edit-distance
        // hint (it will always throw).
        findFunction(name); // throws
    }
    const size_t matchedFunctions = nodes.size();

    struct Row
    {
        std::string caller, callee;
        const CallICFGNode* cs;
        bool direct;
    };
    std::vector<Row> rows;
    for (const CallGraphNode* node : nodes)
    {
        const auto& edges = incoming ? node->getInEdges() : node->getOutEdges();
        for (const CallGraphEdge* e : edges)
        {
            // CallGraphNode names can differ from FunObjVar names only when
            // fun is null, which addCallGraphNode never produces; keep
            // getFunction() for symmetry with functions().
            const std::string caller = e->getSrcNode()->getFunction()->getName();
            const std::string callee = e->getDstNode()->getFunction()->getName();
            for (auto it = e->directCallsBegin(); it != e->directCallsEnd();
                 ++it)
                rows.push_back({caller, callee, *it, true});
            for (auto it = e->indirectCallsBegin();
                 it != e->indirectCallsEnd(); ++it)
                rows.push_back({caller, callee, *it, false});
        }
    }
    // Callsite sets are unordered; sort by (callsite id, callee) so output is
    // stable across runs even after merging rows from multiple nodes.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b)
    {
        if (a.cs->getId() != b.cs->getId())
            return a.cs->getId() < b.cs->getId();
        return a.callee < b.callee;
    });
    constexpr size_t kCap = 200;
    size_t total = rows.size();
    bool truncated = rows.size() > kCap;
    if (truncated)
        rows.resize(kCap);
    json calls = json::array();
    for (const Row& r : rows)
        calls.push_back({{"caller", r.caller},
                         {"callee", r.callee},
                         {"callsite", evidence::node(r.cs)},
                         {"direct", r.direct}});
    return json{{"function", name}, {"calls", std::move(calls)},
                {"total", total}, {"truncated", truncated},
                {"matched_functions", matchedFunctions}};
}

json QueryEngine::schemaQ(const json&) const
{
    json j = schema::registry();
    // "implemented" comes from the live method table, not from Schema.cpp, so
    // the schema stays honest as methods land without anyone updating a flag.
    const std::vector<std::string> impl = methodNames();
    for (json& m : j["methods"])
    {
        const std::string name = m["name"].get<std::string>();
        m["implemented"] =
            std::find(impl.begin(), impl.end(), name) != impl.end();
    }
    j["program"] = json{{"modules", modules}, {"summary", summary()}};
    return j;
}

std::vector<const SVFVar*> QueryEngine::resolveVars(const json& spec) const
{
    if (!spec.is_object())
        throw std::runtime_error(std::string("var anchor must be an object; ") +
                                 kAnchorForms);

    std::vector<const SVFVar*> out;
    std::set<NodeID> seen;
    auto add = [&](const SVFVar* v)
    {
        if (v && seen.insert(v->getId()).second)
            out.push_back(v);
    };

    if (spec.contains("file") || spec.contains("line"))
    {
        if (!spec.contains("file") || !spec["file"].is_string() ||
            !spec.contains("line") || !spec["line"].is_number_integer())
            throw std::runtime_error(
                std::string("file:line anchor needs string \"file\" and "
                            "integer \"line\"; ") + kAnchorForms);
        const std::string file = spec["file"].get<std::string>();
        const int line = spec["line"].get<int>();
        const std::string name = spec.value("name", "");
        // Lines in this file where SOME var is defined — error-hint material.
        std::set<int> definingLines;
        for (const auto& it : *pag->getICFG())
        {
            const ICFGNode* node = it.second;
            const json l = evidence::loc(node->getSourceLoc());
            if (!fileMatches(l["file"].get_ref<const std::string&>(), file))
                continue;
            const int nodeLine = l["line"].get<int>();
            if (!node->getSVFStmts().empty() && nodeLine > 0)
                definingLines.insert(nodeLine);
            if (nodeLine != line)
                continue;
            for (const SVFStmt* stmt : node->getSVFStmts())
            {
                const SVFVar* def = stmt->getDstNode();
                if (!def)
                    continue;
                if (!name.empty() &&
                    def->getValueName().find(name) == std::string::npos)
                    continue;
                add(def);
            }
        }
        if (out.empty())
        {
            std::string msg = "no variables";
            if (!name.empty())
                msg += " named like '" + name + "'";
            msg += " defined at file " + file + " line " +
                   std::to_string(line);
            if (!definingLines.empty())
            {
                // Up to 5 nearest lines that DO define vars in this file.
                std::vector<int> lines(definingLines.begin(),
                                       definingLines.end());
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
                msg += "; nearest lines with definitions:";
                for (int ln : lines)
                    msg += " " + std::to_string(ln);
            }
            throw std::runtime_error(msg + ". " + kAnchorForms);
        }
    }
    else if (spec.contains("func"))
    {
        if (!spec["func"].is_string())
            throw std::runtime_error(
                std::string("func anchor needs a string \"func\"; ") +
                kAnchorForms);
        const std::string fname = spec["func"].get<std::string>();
        const bool wantRet = spec.value("ret", false);
        const bool hasArg = spec.contains("arg");
        if (wantRet == hasArg) // both set, or neither
            throw std::runtime_error(
                std::string("func anchor needs exactly one of \"ret\": true "
                            "or \"arg\": <N>; ") + kAnchorForms);
        if (hasArg && (!spec["arg"].is_number_integer() ||
                       spec["arg"].get<int>() < 0))
            throw std::runtime_error(
                std::string("\"arg\" must be a non-negative integer; ") +
                kAnchorForms);
        const std::vector<const CallGraphNode*> nodes = findFunctions(fname);
        if (nodes.empty())
            findFunction(fname); // throws with the did-you-mean hint
        size_t callsites = 0;
        for (const CallGraphNode* node : nodes)
        {
            for (const CallGraphEdge* e : node->getInEdges())
            {
                auto eachCallsite = [&](const CallICFGNode* cs)
                {
                    ++callsites;
                    if (wantRet)
                    {
                        const RetICFGNode* ret = cs->getRetICFGNode();
                        if (pag->callsiteHasRet(ret))
                            add(pag->getCallSiteRet(ret));
                    }
                    else
                    {
                        const u32_t argNo = spec["arg"].get<u32_t>();
                        if (argNo < cs->arg_size())
                            add(cs->getArgument(argNo));
                    }
                };
                for (auto it = e->directCallsBegin();
                     it != e->directCallsEnd(); ++it)
                    eachCallsite(*it);
                for (auto it = e->indirectCallsBegin();
                     it != e->indirectCallsEnd(); ++it)
                    eachCallsite(*it);
            }
        }
        if (out.empty())
        {
            std::string what = wantRet
                ? "no callsite of '" + fname + "' uses its return value"
                : "no callsite of '" + fname + "' has an argument #" +
                      spec["arg"].dump();
            what += " (" + std::to_string(callsites) + " callsite(s) found). ";
            throw std::runtime_error(what + kAnchorForms);
        }
    }
    else
    {
        throw std::runtime_error(
            std::string("unrecognized var anchor ") + spec.dump() + "; " +
            kAnchorForms);
    }

    // Callsite sets are unordered: sort by id for deterministic output.
    std::sort(out.begin(), out.end(),
              [](const SVFVar* a, const SVFVar* b)
    {
        return a->getId() < b->getId();
    });
    return out;
}

json QueryEngine::cfg(const json& params) const
{
    const std::string name = requiredString(params, "func");
    // Singular lookup: a CFG belongs to ONE function, so ambiguity (same-named
    // statics across TUs) is a real error here, unlike callers/callees.
    const FunObjVar* fun = findFunction(name)->getFunction();

    std::vector<const ICFGNode*> nodes;
    for (const auto& it : *pag->getICFG())
        if (it.second->getFun() == fun)
            nodes.push_back(it.second);
    // ICFG iteration is id-ordered already (OrderedMap), so `nodes` is sorted.

    struct Edge
    {
        NodeID src, dst;
        const char* kind;
    };
    std::vector<Edge> edges;
    for (const ICFGNode* n : nodes)
    {
        for (const ICFGEdge* e : n->getOutEdges())
        {
            const char* kind = SVFUtil::isa<CallCFGEdge>(e) ? "CallCFGEdge"
                               : SVFUtil::isa<RetCFGEdge>(e)
                                   ? "RetCFGEdge"
                                   : "IntraCFGEdge";
            edges.push_back({n->getId(), e->getDstNode()->getId(), kind});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b)
    {
        if (a.src != b.src)
            return a.src < b.src;
        return a.dst < b.dst;
    });

    const size_t totalNodes = nodes.size(), totalEdges = edges.size();
    const bool nodesTruncated = totalNodes > kCfgNodeCap;
    const bool edgesTruncated = totalEdges > kCfgEdgeCap;
    if (nodesTruncated)
        nodes.resize(kCfgNodeCap);
    if (edgesTruncated)
        edges.resize(kCfgEdgeCap);

    json jnodes = json::array();
    for (const ICFGNode* n : nodes)
        jnodes.push_back(evidence::node(n));
    json jedges = json::array();
    for (const Edge& e : edges)
        jedges.push_back({{"src", e.src}, {"dst", e.dst}, {"kind", e.kind}});
    return json{{"function", name},
                {"nodes", std::move(jnodes)},
                {"edges", std::move(jedges)},
                {"total_nodes", totalNodes},
                {"total_edges", totalEdges},
                {"truncated", nodesTruncated || edgesTruncated}};
}

json QueryEngine::defuse(const json& params) const
{
    std::vector<const SVFVar*> vars = resolveVars(requiredVarAnchor(params));
    const size_t total = vars.size();
    const bool truncated = total > kVarCap;
    if (truncated)
        vars.resize(kVarCap);
    json rows = json::array();
    for (const SVFVar* v : vars)
    {
        // In SVFIR the SVFStmts ARE the var's graph edges: in-edges define
        // the var (Addr/Copy/Load/... into it), out-edges consume it.
        json defs = json::array(), uses = json::array();
        for (const SVFStmt* s : sortedStmts(v->getInEdges()))
            defs.push_back(stmtRecord(s));
        for (const SVFStmt* s : sortedStmts(v->getOutEdges()))
            uses.push_back(stmtRecord(s));
        rows.push_back({{"var", evidence::node(v)},
                        {"defs", std::move(defs)},
                        {"uses", std::move(uses)}});
    }
    return json{{"vars", std::move(rows)}, {"total", total},
                {"truncated", truncated}};
}

json QueryEngine::pts(const json& params) const
{
    std::vector<const SVFVar*> vars = resolveVars(requiredVarAnchor(params));
    const size_t total = vars.size();
    const bool truncated = total > kVarCap;
    if (truncated)
        vars.resize(kVarCap);
    json rows = json::array();
    for (const SVFVar* v : vars)
    {
        json objs = json::array();
        // PointsTo iterates ascending object ids — deterministic.
        for (NodeID objId : ander->getPts(v->getId()))
            objs.push_back(evidence::node(pag->getGNode(objId)));
        rows.push_back({{"var", evidence::node(v)},
                        {"points_to", std::move(objs)}});
    }
    return json{{"vars", std::move(rows)}, {"total", total},
                {"truncated", truncated}};
}

json QueryEngine::aliases(const json& params) const
{
    std::vector<const SVFVar*> vars = resolveVars(requiredVarAnchor(params));
    const size_t total = vars.size();
    bool truncated = total > kVarCap;
    if (truncated)
        vars.resize(kVarCap);
    json rows = json::array();
    for (const SVFVar* v : vars)
    {
        // v0 candidate scope (documented limitation, see schema): ValVars of
        // the var's OWN function. Formal params are ArgValVars of the same
        // function, so they are covered by this filter. Vars without an
        // enclosing function (globals/dummies) get an empty list.
        json arr = json::array();
        size_t kept = 0;
        if (const FunObjVar* fn = v->getFunction())
        {
            for (const auto& it : *pag) // id-ordered
            {
                const SVFVar* cand = it.second;
                if (cand->getId() == v->getId()) // exclude the var itself
                    continue;
                if (!SVFUtil::isa<ValVar>(cand) || cand->getFunction() != fn)
                    continue;
                if (ander->alias(v->getId(), cand->getId()) == NoAlias)
                    continue;
                if (kept == kAliasCap)
                {
                    truncated = true;
                    break;
                }
                arr.push_back(evidence::node(cand));
                ++kept;
            }
        }
        rows.push_back({{"var", evidence::node(v)},
                        {"aliases", std::move(arr)}});
    }
    return json{{"vars", std::move(rows)}, {"total", total},
                {"truncated", truncated}};
}
