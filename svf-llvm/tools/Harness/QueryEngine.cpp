//===- QueryEngine.cpp -- SVF analysis bootstrap + query dispatch --------===//
//
// Bootstrap, dispatch and the function-lookup helpers. The per-method query
// bodies live in Queries.cpp (same class, second TU).
//
//===----------------------------------------------------------------------===//
#include "QueryEngine.h"
#include "CFL/CFLAlias.h"
#include "DDA/DDAClient.h"
#include "DDA/FlowDDA.h"
#include "Graphs/CallGraph.h"
#include "Graphs/SVFG.h"
#include "MTA/MTA.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "Util/Options.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

using namespace SVF;
using json = nlohmann::json;

QueryEngine::HarnessConfig::HarnessConfig()
    : pointerAnalysis("andersen-wave-diff"),
      svfgMode("full"),
      svfgIndirectCalls(Options::SVFGWithIndirectCall()),
      svfgPostOpts(Options::OPTSVFG())
{
}

QueryEngine::HarnessConfig QueryEngine::HarnessConfig::fromJson(const json& j)
{
    HarnessConfig c;
    if (j.is_null())
        return c;
    if (!j.is_object())
        throw std::runtime_error("analysis config must be a JSON object");
    if (j.contains("pointer_analysis"))
    {
        if (!j["pointer_analysis"].is_string())
            throw std::runtime_error("pointer_analysis must be a string");
        c.pointerAnalysis = j["pointer_analysis"].get<std::string>();
        if (c.pointerAnalysis != "andersen-wave-diff")
            throw std::runtime_error("unknown pointer_analysis '" +
                                     c.pointerAnalysis +
                                     "' (supported: andersen-wave-diff)");
    }
    if (j.contains("svfg"))
    {
        const json& s = j["svfg"];
        if (!s.is_object())
            throw std::runtime_error("svfg config must be an object");
        if (s.contains("mode"))
        {
            if (!s["mode"].is_string())
                throw std::runtime_error("svfg.mode must be a string");
            c.svfgMode = s["mode"].get<std::string>();
            if (c.svfgMode != "full" && c.svfgMode != "ptr-only")
                throw std::runtime_error("unknown svfg.mode '" + c.svfgMode +
                                         "' (supported: full, ptr-only)");
        }
        if (s.contains("indirect_calls"))
        {
            if (!s["indirect_calls"].is_boolean())
                throw std::runtime_error("svfg.indirect_calls must be a boolean");
            c.svfgIndirectCalls = s["indirect_calls"].get<bool>();
        }
        if (s.contains("post_opts"))
        {
            if (!s["post_opts"].is_boolean())
                throw std::runtime_error("svfg.post_opts must be a boolean");
            c.svfgPostOpts = s["post_opts"].get<bool>();
        }
    }
    return c;
}

json QueryEngine::HarnessConfig::toJson() const
{
    return json{
        {"pointer_analysis", json{
            {"active", pointerAnalysis},
            {"supported", json::array({"andersen-wave-diff"})},
        }},
        {"svfg", json{
            {"mode", svfgMode},
            {"supported_modes", json::array({"full", "ptr-only"})},
            {"indirect_calls", svfgIndirectCalls},
            {"post_opts", svfgPostOpts},
        }},
        {"surfaces", json::array({
            {{"name", "wpa"}, {"status", "active-core"},
             {"notes", "Harness currently runs AndersenWaveDiff directly; broader WPA selections are planned."}},
            {{"name", "dda"}, {"status", "supported"},
             {"notes", "Lazy FlowDDA surface is available via dda_pts and dda_aliases."}},
            {{"name", "cfl"}, {"status", "supported"},
             {"notes", "Lazy CFLAlias surface is available via cfl_pts and cfl_aliases."}},
            {{"name", "saber"}, {"status", "supported"},
             {"notes", "SABER checker summaries are available via saber_leaks, saber_double_frees, and saber_file_leaks."}},
            {{"name", "mta"}, {"status", "supported"},
             {"notes", "Lazy MTA thread/MHP summaries are available via mta_summary and mta_mhp."}},
            {{"name", "ae"}, {"status", "supported"},
             {"notes", "Lazy Abstract Execution trace/state inspection is available via ae_summary and ae_state. Detector bug summaries are future work."}},
        })},
    };
}

QueryEngine::QueryEngine(const std::vector<std::string>& moduleNames)
    : QueryEngine(moduleNames, HarnessConfig())
{
}

QueryEngine::QueryEngine(const std::vector<std::string>& moduleNames,
                         const HarnessConfig& cfg)
    : config(cfg)
{
    if (moduleNames.empty())
        throw std::runtime_error("no input bitcode module given");
    // Pre-validate: SVF core abort()s on unreadable/invalid inputs, so fail
    // early with a catchable error instead.
    for (const std::string& name : moduleNames)
    {
        if (!std::ifstream(name).good())
            throw std::runtime_error("cannot read module: " + name);
        // LLVMUtil::isIRFile also rejects directories (ifstream on a dir is
        // good() on Linux) and non-IR text files.
        if (!LLVMUtil::isIRFile(name))
            throw std::runtime_error("not an LLVM IR file: " + name);
    }
    modules = moduleNames;
    std::vector<std::string> names(moduleNames);
    LLVMModuleSet::preProcessBCs(names);
    LLVMModuleSet::buildSVFModule(names);

    // SVFIRBuilder holds a non-owning pointer to the SVFIR::getPAG()
    // singleton, so a temporary builder is safe here.
    SVFIRBuilder builder;
    pag = builder.build();
    ander = AndersenWaveDiff::createAndersenWaveDiff(pag);
    callgraph = ander->getCallGraph();
    // svfBuilder is a member: it owns the SVFG via unique_ptr and must
    // outlive our svfg pointer (a stack-local builder left it dangling).
    svfBuilder = std::make_unique<SVFGBuilder>(config.svfgIndirectCalls,
                                               config.svfgPostOpts);
    if (config.svfgMode == "full")
        svfg = svfBuilder->buildFullSVFG(ander);
    else if (config.svfgMode == "ptr-only")
        svfg = svfBuilder->buildPTROnlySVFG(ander);
    else
        throw std::runtime_error("unknown svfg.mode '" + config.svfgMode +
                                 "' (supported: full, ptr-only)");
}

QueryEngine::~QueryEngine() = default;

json QueryEngine::summary() const
{
    json j;
    j["functions"] = callgraph->getTotalNodeNum();
    j["icfg_nodes"] = pag->getICFG()->getTotalNodeNum();
    j["svfg_nodes"] = svfg->getTotalNodeNum();
    j["pag_nodes"] = pag->getTotalNodeNum();
    return j;
}

namespace
{
std::string findDefaultCFLGrammar()
{
    std::vector<std::string> candidates;
#ifdef SVF_HARNESS_CFL_GRAMMAR_DIR
    candidates.push_back(std::string(SVF_HARNESS_CFL_GRAMMAR_DIR) +
                         "/PAGGrammar.txt");
#endif
    candidates.push_back("svf/include/CFL/grammar/PAGGrammar.txt");
    candidates.push_back("../svf/include/CFL/grammar/PAGGrammar.txt");
    for (const std::string& path : candidates)
        if (std::ifstream(path).good())
            return path;
    throw std::runtime_error(
        "cannot locate CFL grammar PAGGrammar.txt; set SVF's -grammar option");
}

void ensureCFLDefaults()
{
    if (Options::GrammarFilename().empty())
        const_cast<Option<std::string>&>(Options::GrammarFilename)
            .setValue(findDefaultCFLGrammar());
    const_cast<Option<bool>&>(Options::EnableAliasCheck).setValue(false);
}

void ensureDDADefaults()
{
    // Query mode must keep stdout as a single JSON document.
    const_cast<Option<bool>&>(Options::PStat).setValue(false);
}

/// Iterative two-row Levenshtein distance for the unknown-function hint.
/// Inputs are capped at 64 chars so a pathological name stays O(1)-ish.
size_t editDistance(const std::string& fullA, const std::string& fullB)
{
    const std::string a = fullA.substr(0, 64), b = fullB.substr(0, 64);
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                               prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}
} // namespace

std::vector<const CallGraphNode*>
QueryEngine::findFunctions(const std::string& name) const
{
    std::vector<const CallGraphNode*> matches;
    for (const auto& it : *callgraph)
    {
        const CallGraphNode* node = it.second;
        if (!node->getFunction())
            continue;
        if (node->getFunction()->getName() == name)
            matches.push_back(node);
    }
    return matches;
}

const CallGraphNode* QueryEngine::findFunction(const std::string& name) const
{
    // Collect all unique candidate names for the hint, simultaneously
    // checking for exact matches.
    std::set<std::string> candidateSet;
    std::vector<const CallGraphNode*> matches;
    for (const auto& it : *callgraph)
    {
        const CallGraphNode* node = it.second;
        if (!node->getFunction())
            continue;
        const std::string& fname = node->getFunction()->getName();
        if (fname == name)
            matches.push_back(node);
        else
            candidateSet.insert(fname);
    }
    if (matches.size() == 1)
        return matches[0];
    if (matches.size() > 1)
    {
        throw std::runtime_error(
            "ambiguous function name '" + name + "': " +
            std::to_string(matches.size()) +
            " matches; use functions() to disambiguate");
    }
    // Miss: suggest the 5 closest names.
    // Compute edit-distance ONCE per unique candidate (Fix 3: avoid
    // recomputing inside a sort comparator which would be O(n log n) DP runs).
    std::vector<std::pair<size_t, std::string>> scored;
    scored.reserve(candidateSet.size());
    for (const std::string& cand : candidateSet)
        scored.emplace_back(editDistance(name, cand), cand);
    // partial_sort: only the first 5 by (distance, name) — O(n log 5).
    const size_t topN = std::min<size_t>(5, scored.size());
    std::partial_sort(scored.begin(), scored.begin() + topN, scored.end(),
                      [](const std::pair<size_t, std::string>& a,
                         const std::pair<size_t, std::string>& b)
    {
        if (a.first != b.first)
            return a.first < b.first;
        return a.second < b.second;
    });
    scored.resize(topN);
    std::string msg = "unknown function '" + name + "'";
    if (!scored.empty())
    {
        msg += "; did you mean: ";
        for (size_t i = 0; i < scored.size(); ++i)
            msg += (i ? ", " : "") + scored[i].second;
        msg += "?";
    }
    throw std::runtime_error(msg);
}

const std::vector<QueryEngine::Method>& QueryEngine::methodTable()
{
    static const std::vector<Method> table = {
        {"schema", &QueryEngine::schemaQ},
        {"summary", &QueryEngine::summaryQ},
        {"functions", &QueryEngine::functions},
        {"callers", &QueryEngine::callers},
        {"callees", &QueryEngine::callees},
        {"cfg", &QueryEngine::cfg},
        {"defuse", &QueryEngine::defuse},
        {"pts", &QueryEngine::pts},
        {"aliases", &QueryEngine::aliases},
        {"cfl_pts", &QueryEngine::cflPts},
        {"cfl_aliases", &QueryEngine::cflAliases},
        {"dda_pts", &QueryEngine::ddaPts},
        {"dda_aliases", &QueryEngine::ddaAliases},
        {"saber_leaks", &QueryEngine::saberLeaks},
        {"saber_double_frees", &QueryEngine::saberDoubleFrees},
        {"saber_file_leaks", &QueryEngine::saberFileLeaks},
        {"mta_summary", &QueryEngine::mtaSummary},
        {"mta_mhp", &QueryEngine::mtaMHP},
        {"ae_summary", &QueryEngine::aeSummary},
        {"ae_state", &QueryEngine::aeState},
        {"vfpath", &QueryEngine::vfpath},
        {"reachable", &QueryEngine::reachable},
        {"graphs", &QueryEngine::graphs},
        {"graph_nodes", &QueryEngine::graphNodes},
        {"graph_edges", &QueryEngine::graphEdges},
        {"node", &QueryEngine::nodeQ},
        {"neighbors", &QueryEngine::neighbors},
        {"analysis_config", &QueryEngine::analysisConfig},
    };
    return table;
}

std::vector<std::string> QueryEngine::methodNames()
{
    std::vector<std::string> names;
    for (const Method& m : methodTable())
        names.push_back(m.name);
    return names;
}

json QueryEngine::dispatch(const std::string& m, const json& p)
{
    for (const Method& entry : methodTable())
        if (m == entry.name)
            return (this->*entry.handler)(p);
    throw std::runtime_error("unknown method: " + m);
}

json QueryEngine::analysisConfig(const json&) const
{
    return config.toJson();
}

CFLAlias* QueryEngine::getCFLAlias() const
{
    if (!cflAlias)
    {
        ensureCFLDefaults();
        cflAlias = std::make_unique<CFLAlias>(pag);
        cflAlias->analyze();
    }
    return cflAlias.get();
}

FlowDDA* QueryEngine::getFlowDDA() const
{
    if (!flowDDA)
    {
        ensureDDADefaults();
        ddaClient = std::make_unique<DDAClient>();
        flowDDA = std::make_unique<FlowDDA>(pag, ddaClient.get());
        flowDDA->initialize();
    }
    return flowDDA.get();
}
