//===- QueryEngine.h -- SVF analysis bootstrap + query dispatch ----------===//
#pragma once
#include "MSSA/SVFGBuilder.h"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

namespace SVF
{
class SVFIR;
class SVFG;
class AndersenBase;
class CallGraph;
class CallGraphNode;
}

class QueryEngine
{
public:
    /// Builds LLVM modules -> SVFIR -> Andersen -> SVFG. Throws std::runtime_error on bad input.
    explicit QueryEngine(const std::vector<std::string>& moduleNames);
    nlohmann::json dispatch(const std::string& method, const nlohmann::json& params);
    /// Names of all dispatchable methods, in registration order. Backed by the
    /// same table dispatch() uses, so the two can never drift apart.
    static std::vector<std::string> methodNames();
    nlohmann::json summary() const;
    /// List functions whose name matches params["pattern"] (ECMAScript regex,
    /// search semantics; missing/empty => all). Sorted by (name, node-id) for
    /// determinism across TUs. Result includes "total" (pre-cap match count),
    /// "truncated" bool, and "functions" array; capped at 200 entries.
    nlohmann::json functions(const nlohmann::json& params) const;
    /// Call-graph navigation for params["func"] (exact name). One result row
    /// per callsite (a CallGraphEdge merges all calls between two functions;
    /// we expand its direct/indirect callsite sets), each with callsite
    /// evidence and direct=false for Andersen-resolved function-pointer
    /// calls. Same {calls, total, truncated} cap-at-200 shape as functions().
    nlohmann::json callers(const nlohmann::json& params) const
    {
        return callEdges(params, /*incoming=*/true);
    }
    nlohmann::json callees(const nlohmann::json& params) const
    {
        return callEdges(params, /*incoming=*/false);
    }

    // One instance per process — SVF state (LLVMModuleSet/PAG) is global.
    // Never throw from callbacks passed into SVF/LLVM code.
    QueryEngine(const QueryEngine&) = delete;
    QueryEngine& operator=(const QueryEngine&) = delete;

private:
    /// Method registry entry: every query handler has the uniform signature
    /// json(const json& params) const. dispatch() and methodNames() share it.
    using Handler = nlohmann::json (QueryEngine::*)(const nlohmann::json&) const;
    struct Method
    {
        const char* name;
        Handler handler;
    };
    static const std::vector<Method>& methodTable();
    /// Adapter: summary() takes no params but the table needs the uniform signature.
    nlohmann::json summaryQ(const nlohmann::json&) const
    {
        return summary();
    }
    /// schema::registry() + runtime parts: per-method "implemented" flags
    /// (derived from methodTable, so they can never drift) and the "program"
    /// block (module paths + summary counts) so a client can verify which
    /// daemon/program it is talking to.
    nlohmann::json schemaQ(const nlohmann::json&) const;
    /// Shared body of callers()/callees(): walk the function's call-graph
    /// node IN (incoming=callers) or OUT (callees) edges.
    nlohmann::json callEdges(const nlohmann::json& params, bool incoming) const;
    /// Exact-name lookup over call-graph nodes. On miss throws
    /// std::runtime_error listing up to 5 closest names by edit distance.
    const SVF::CallGraphNode* findFunction(const std::string& name) const;

    /// Module paths as given to the ctor; reported in schema().program.
    std::vector<std::string> modules;
    SVF::SVFIR* pag = nullptr;
    SVF::AndersenBase* ander = nullptr;
    SVF::SVFGBuilder svfBuilder; // owns the SVFG; must outlive svfg
    SVF::SVFG* svfg = nullptr;
    SVF::CallGraph* callgraph = nullptr;
};
