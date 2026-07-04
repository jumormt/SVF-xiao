//===- AEQueries.cpp -- QueryEngine Abstract Execution surface -----------===//
//
// SVF's Abstract Execution (AE) is a trace/state engine with optional bug
// detectors. This adapter exposes the trace and per-node abstract state while
// keeping detector report extraction for a later, explicit reporter contract.
//
//===----------------------------------------------------------------------===//
#include "QueryEngine.h"
#include "Evidence.h"
#include "AE/Core/AbstractState.h"
#include "AE/Svfexe/AbstractInterpretation.h"
#include "Graphs/CallGraph.h"
#include "Graphs/ICFG.h"
#include "Graphs/ICFGNode.h"
#include "SVFIR/SVFIR.h"
#include "Util/Options.h"
#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using namespace SVF;
using json = nlohmann::json;

namespace
{
constexpr size_t kDefaultStateEntryCap = 50;
constexpr size_t kMaxStateEntryCap = 500;
constexpr size_t kStateTextCap = 1000;

void ensureAEDefaults()
{
    const_cast<Option<bool>&>(Options::PStat).setValue(false);
    const_cast<Option<bool>&>(Options::BufferOverflowCheck).setValue(false);
    const_cast<Option<bool>&>(Options::NullDerefCheck).setValue(false);
}

class ScopedFDSilencer
{
public:
    explicit ScopedFDSilencer(int fd) : target(fd)
    {
        std::fflush(target == STDOUT_FILENO ? stdout : stderr);
        saved = dup(target);
        int devNull = open("/dev/null", O_WRONLY);
        if (saved >= 0 && devNull >= 0)
            dup2(devNull, target);
        if (devNull >= 0)
            close(devNull);
    }

    ~ScopedFDSilencer()
    {
        std::fflush(target == STDOUT_FILENO ? stdout : stderr);
        if (saved >= 0)
        {
            dup2(saved, target);
            close(saved);
        }
    }

private:
    int target;
    int saved = -1;
};

std::string aeSparsityName()
{
    switch (Options::AESparsity())
    {
    case AbstractInterpretation::AESparsity::SemiSparse:
        return "semi-sparse";
    case AbstractInterpretation::AESparsity::Sparse:
        return "sparse";
    case AbstractInterpretation::AESparsity::Dense:
    default:
        return "dense";
    }
}

std::string aeEntryName()
{
    return Options::AEFunEntry() == AbstractInterpretation::AEFunEntryMode::NO_MAIN
               ? "no-main"
               : "main";
}

std::string recurModeName()
{
    switch (Options::HandleRecur())
    {
    case AbstractInterpretation::HandleRecur::TOP:
        return "top";
    case AbstractInterpretation::HandleRecur::WIDEN_ONLY:
        return "widen-only";
    case AbstractInterpretation::HandleRecur::WIDEN_NARROW:
    default:
        return "widen-narrow";
    }
}

std::string cappedText(const std::string& text)
{
    if (text.size() <= kStateTextCap)
        return text;
    return text.substr(0, kStateTextCap) + "...";
}

size_t stateLimit(const json& params)
{
    size_t limit = params.value("limit", kDefaultStateEntryCap);
    if (limit < 1 || limit > kMaxStateEntryCap)
        throw std::runtime_error("limit must be in [1, 500]");
    return limit;
}

const json& requiredObject(const json& params, const char* key)
{
    if (!params.contains(key) || !params[key].is_object())
        throw std::runtime_error(std::string("missing required object param: ") +
                                 key);
    return params[key];
}

std::vector<const ICFGNode*> resolveICFGLineAnchor(SVFIR* pag,
                                                   const json& spec)
{
    if (!spec.contains("file") || !spec["file"].is_string() ||
        !spec.contains("line") || !spec["line"].is_number_integer())
        throw std::runtime_error(
            "AE source-location anchor needs {file: string, line: integer, kind?: string}");
    const std::string file = spec["file"].get<std::string>();
    const int line = spec["line"].get<int>();
    const std::string kind = spec.value("kind", "");

    std::vector<const ICFGNode*> nodes;
    std::set<int> nearby;
    for (const auto& it : *pag->getICFG())
    {
        const ICFGNode* node = it.second;
        json rec = evidence::node(node);
        const json& loc = rec["loc"];
        if (!evidence::fileMatches(loc.value("file", ""), file))
            continue;
        const int nodeLine = loc.value("line", 0);
        if (nodeLine > 0)
            nearby.insert(nodeLine);
        if (nodeLine != line)
            continue;
        if (!kind.empty() && rec.value("kind", "") != kind)
            continue;
        nodes.push_back(node);
    }
    std::sort(nodes.begin(), nodes.end(), [](const ICFGNode* a,
                                             const ICFGNode* b)
    {
        return a->getId() < b->getId();
    });
    if (nodes.empty())
    {
        std::string msg = "no ICFG nodes at " + file + ":" +
                          std::to_string(line);
        if (!nearby.empty())
        {
            msg += "; nearby lines:";
            size_t n = 0;
            for (int l : nearby)
            {
                if (n++ == 5)
                    break;
                msg += " " + std::to_string(l);
            }
        }
        throw std::runtime_error(msg);
    }
    return nodes;
}

json valueEntry(SVFIR* pag, NodeID id, const AbstractValue& value,
                bool includeVarEvidence)
{
    json row{{"id", id}, {"value", value.toString()}};
    if (includeVarEvidence && pag->hasGNode(id))
        row["var"] = evidence::node(pag->getSVFVar(id));
    return row;
}

json stateRecord(SVFIR* pag, const AbstractState& state, size_t limit)
{
    json vars = json::array();
    json addrs = json::array();

    size_t emitted = 0;
    for (const auto& it : state.getVarToVal())
    {
        if (emitted++ >= limit)
            break;
        vars.push_back(valueEntry(pag, it.first, it.second,
                                  /*includeVarEvidence=*/true));
    }
    emitted = 0;
    for (const auto& it : state.getLocToVal())
    {
        if (emitted++ >= limit)
            break;
        addrs.push_back(valueEntry(pag, it.first, it.second,
                                   /*includeVarEvidence=*/pag->hasGNode(it.first)));
    }

    const size_t varTotal = state.getVarToVal().size();
    const size_t addrTotal = state.getLocToVal().size();
    return json{{"vars_total", varTotal},
                {"addrs_total", addrTotal},
                {"vars", std::move(vars)},
                {"addrs", std::move(addrs)},
                {"truncated", varTotal > limit || addrTotal > limit},
                {"text", cappedText(state.toString())}};
}
} // namespace

AbstractInterpretation* QueryEngine::getAE() const
{
    if (!aeReady)
    {
        ensureAEDefaults();
        AbstractInterpretation& instance = AbstractInterpretation::getAEInstance();
        ae = &instance;
        ScopedFDSilencer out(STDOUT_FILENO);
        ScopedFDSilencer err(STDERR_FILENO);
        ae->runOnModule();
        aeReady = true;
    }
    return ae;
}

json QueryEngine::aeSummary(const json&) const
{
    AbstractInterpretation* analysis = getAE();
    auto& trace = analysis->getTrace();

    size_t varEntries = 0;
    size_t addrEntries = 0;
    std::set<const FunObjVar*> funcs;
    for (const auto& it : trace)
    {
        varEntries += it.second.getVarToVal().size();
        addrEntries += it.second.getLocToVal().size();
        if (it.first && it.first->getFun())
            funcs.insert(it.first->getFun());
    }

    const size_t totalICFG = pag->getICFG()->getTotalNodeNum();
    const double coverage =
        totalICFG ? (100.0 * static_cast<double>(trace.size()) /
                     static_cast<double>(totalICFG)) : 0.0;

    return json{{"analysis", "ae"},
                {"sparsity", aeSparsityName()},
                {"entry", aeEntryName()},
                {"recursion", recurModeName()},
                {"trace_nodes", trace.size()},
                {"total_icfg_nodes", totalICFG},
                {"icfg_coverage_percent", coverage},
                {"analyzed_functions", funcs.size()},
                {"total_functions", callgraph->getTotalNodeNum()},
                {"var_entries", varEntries},
                {"addr_entries", addrEntries}};
}

json QueryEngine::aeState(const json& params) const
{
    AbstractInterpretation* analysis = getAE();
    const size_t limit = stateLimit(params);
    std::vector<const ICFGNode*> nodes =
        resolveICFGLineAnchor(pag, requiredObject(params, "at"));

    const size_t total = nodes.size();
    bool truncated = nodes.size() > 50;
    if (nodes.size() > 50)
        nodes.resize(50);

    json states = json::array();
    for (const ICFGNode* node : nodes)
    {
        json row{{"node", evidence::node(node)},
                 {"has_state", analysis->hasAbsState(node)}};
        if (analysis->hasAbsState(node))
            row["state"] = stateRecord(pag, analysis->getAbsState(node), limit);
        states.push_back(std::move(row));
    }

    return json{{"analysis", "ae"},
                {"matches", total},
                {"states", std::move(states)},
                {"limit", limit},
                {"truncated", truncated}};
}
