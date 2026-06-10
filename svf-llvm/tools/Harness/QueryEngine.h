//===- QueryEngine.h -- SVF analysis bootstrap + query dispatch ----------===//
#pragma once
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

namespace SVF
{
class SVFIR;
class SVFG;
class AndersenBase;
class CallGraph;
}

class QueryEngine
{
public:
    /// Builds LLVM modules -> SVFIR -> Andersen -> SVFG. Throws std::runtime_error on bad input.
    explicit QueryEngine(const std::vector<std::string>& moduleNames);
    nlohmann::json dispatch(const std::string& method, const nlohmann::json& params);
    nlohmann::json summary() const;

    // One instance per process — SVF state (LLVMModuleSet/PAG) is global.
    // Never throw from callbacks passed into SVF/LLVM code.
    QueryEngine(const QueryEngine&) = delete;
    QueryEngine& operator=(const QueryEngine&) = delete;

private:
    SVF::SVFIR* pag = nullptr;
    SVF::AndersenBase* ander = nullptr;
    SVF::SVFG* svfg = nullptr;
    SVF::CallGraph* callgraph = nullptr;
};
