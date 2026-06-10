//===- QueryEngine.cpp -- SVF analysis bootstrap + query dispatch --------===//
//
// Bootstrap, dispatch and the function-lookup helpers. The per-method query
// bodies live in Queries.cpp (same class, second TU).
//
//===----------------------------------------------------------------------===//
#include "QueryEngine.h"
#include "Graphs/CallGraph.h"
#include "Graphs/SVFG.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

using namespace SVF;
using json = nlohmann::json;

QueryEngine::QueryEngine(const std::vector<std::string>& moduleNames)
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
    svfg = svfBuilder.buildFullSVFG(ander);
}

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
