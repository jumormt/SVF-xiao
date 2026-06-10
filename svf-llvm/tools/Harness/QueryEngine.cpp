//===- QueryEngine.cpp -- SVF analysis bootstrap + query dispatch --------===//
#include "QueryEngine.h"
#include "Evidence.h"
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
    std::vector<std::string> names(moduleNames);
    LLVMModuleSet::preProcessBCs(names);
    LLVMModuleSet::buildSVFModule(names);

    SVFIRBuilder builder;
    pag = builder.build();
    ander = AndersenWaveDiff::createAndersenWaveDiff(pag);
    callgraph = ander->getCallGraph();
    SVFGBuilder svfBuilder;
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

const std::vector<QueryEngine::Method>& QueryEngine::methodTable()
{
    static const std::vector<Method> table = {
        {"summary", &QueryEngine::summaryQ},
        {"functions", &QueryEngine::functions},
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
