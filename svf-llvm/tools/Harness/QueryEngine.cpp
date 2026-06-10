//===- QueryEngine.cpp -- SVF analysis bootstrap + query dispatch --------===//
#include "QueryEngine.h"
#include "Evidence.h"
#include "Schema.h"
#include "Graphs/CallGraph.h"
#include "Graphs/SVFG.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include <algorithm>
#include <fstream>
#include <regex>
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

const CallGraphNode* QueryEngine::findFunction(const std::string& name) const
{
    std::vector<std::string> candidates;
    for (const auto& it : *callgraph)
    {
        const CallGraphNode* node = it.second;
        if (!node->getFunction())
            continue;
        if (node->getFunction()->getName() == name)
            return node;
        candidates.push_back(node->getFunction()->getName());
    }
    // Miss: suggest the 5 closest names. Dedup first (same-named statics),
    // then order by (distance, name) for a deterministic hint.
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const std::string& a, const std::string& b)
    {
        return editDistance(name, a) < editDistance(name, b);
    });
    if (candidates.size() > 5)
        candidates.resize(5);
    std::string msg = "unknown function '" + name + "'";
    if (!candidates.empty())
    {
        msg += "; did you mean: ";
        for (size_t i = 0; i < candidates.size(); ++i)
            msg += (i ? ", " : "") + candidates[i];
        msg += "?";
    }
    throw std::runtime_error(msg);
}

json QueryEngine::callEdges(const json& params, bool incoming) const
{
    if (!params.contains("func") || !params["func"].is_string())
        throw std::runtime_error("missing required string param: func");
    const std::string name = params["func"].get<std::string>();
    const CallGraphNode* node = findFunction(name);

    struct Row
    {
        std::string caller, callee;
        const CallICFGNode* cs;
        bool direct;
    };
    std::vector<Row> rows;
    const auto& edges = incoming ? node->getInEdges() : node->getOutEdges();
    for (const CallGraphEdge* e : edges)
    {
        // CallGraphNode names can differ from FunObjVar names only when fun
        // is null, which addCallGraphNode never produces; keep getFunction()
        // for symmetry with functions().
        const std::string caller = e->getSrcNode()->getFunction()->getName();
        const std::string callee = e->getDstNode()->getFunction()->getName();
        for (auto it = e->directCallsBegin(); it != e->directCallsEnd(); ++it)
            rows.push_back({caller, callee, *it, true});
        for (auto it = e->indirectCallsBegin(); it != e->indirectCallsEnd();
             ++it)
            rows.push_back({caller, callee, *it, false});
    }
    // Callsite sets are unordered; sort by (callsite id, callee) so output is
    // stable across runs (one callsite can have several indirect callees).
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
                {"total", total}, {"truncated", truncated}};
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

const std::vector<QueryEngine::Method>& QueryEngine::methodTable()
{
    static const std::vector<Method> table = {
        {"schema", &QueryEngine::schemaQ},
        {"summary", &QueryEngine::summaryQ},
        {"functions", &QueryEngine::functions},
        {"callers", &QueryEngine::callers},
        {"callees", &QueryEngine::callees},
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
