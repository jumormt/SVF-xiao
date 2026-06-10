//===- QueryEngine.cpp -- SVF analysis bootstrap + query dispatch --------===//
#include "QueryEngine.h"
#include "Graphs/SVFG.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include <fstream>
#include <stdexcept>

using namespace SVF;
using json = nlohmann::json;

QueryEngine::QueryEngine(const std::vector<std::string>& moduleNames)
{
    if (moduleNames.empty())
        throw std::runtime_error("no input bitcode module given");
    // Pre-validate: SVF core abort()s on unreadable inputs, so fail early
    // with a catchable error instead.
    for (const std::string& name : moduleNames)
    {
        if (!std::ifstream(name).good())
            throw std::runtime_error("cannot read module: " + name);
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

json QueryEngine::dispatch(const std::string& m, const json& p)
{
    (void)p;
    if (m == "summary")
        return summary();
    throw std::runtime_error("unknown method: " + m);
}
