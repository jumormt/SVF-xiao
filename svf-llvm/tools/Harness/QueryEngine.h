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
class SVFVar;
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
    /// Control-flow graph of params["func"] (exact name; throws on ambiguity
    /// — pick by functions() first). All ICFG nodes of the function plus
    /// their outgoing edges labelled IntraCFGEdge/CallCFGEdge/RetCFGEdge.
    /// Nodes capped at 500, edges at 1000; "truncated" reflects either cap.
    nlohmann::json cfg(const nlohmann::json& params) const;
    /// Def/use statements of each var resolved from params["var"] (see
    /// resolveVars). defs = SVFStmt in-edges, uses = out-edges; each emitted
    /// as {stmt: <SVFStmt kind>, at: <evidence of its ICFG node>}.
    nlohmann::json defuse(const nlohmann::json& params) const;
    /// Andersen may-points-to set of each var resolved from params["var"]:
    /// the abstract objects (ObjVar-family) it may target.
    nlohmann::json pts(const nlohmann::json& params) const;
    /// May-aliases of each var resolved from params["var"]. v0 candidate
    /// scope: ValVars of the var's OWN function only (documented limitation);
    /// vars with no enclosing function get an empty list. Capped at 50
    /// aliases per var.
    nlohmann::json aliases(const nlohmann::json& params) const;

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
    /// node IN (incoming=callers) or OUT (callees) edges. Merges results
    /// from ALL name-matching nodes (handles same-named statics across TUs)
    /// and adds "matched_functions": N to the result (1 in the common case).
    nlohmann::json callEdges(const nlohmann::json& params, bool incoming) const;
    /// Returns ALL call-graph nodes whose function name exactly matches
    /// ``name``. Returns an empty vector only when there are no matches
    /// (the caller is responsible for raising the error). Never throws.
    std::vector<const SVF::CallGraphNode*>
    findFunctions(const std::string& name) const;
    /// Exact-name lookup over call-graph nodes. On miss throws
    /// std::runtime_error listing up to 5 closest names by edit distance.
    /// On ambiguity (multiple statics with the same name) throws
    /// std::runtime_error with a clear message directing the caller to
    /// use functions() to disambiguate. Prefer findFunctions() + merge
    /// semantics for callers/callees; reserve findFunction() for future
    /// single-target uses that must reject ambiguity.
    const SVF::CallGraphNode* findFunction(const std::string& name) const;
    /// Resolve a JSON "var" anchor to SVFVar(s). Accepted forms:
    ///  {"file": "demo.c", "line": 8}             — all SVFVars whose def
    ///      stmt sits at that line (file matched by path suffix)
    ///  {"file": ..., "line": N, "name": "b"}     — additionally filter by
    ///      LLVM value-name substring
    ///  {"func": "malloc", "ret": true}           — return-value vars at
    ///      every callsite of func
    ///  {"func": "memcpy", "arg": 0}              — actual-argument vars at
    ///      every callsite of func
    /// Result is deduped and sorted by node id. Empty resolution throws
    /// std::runtime_error listing the accepted forms (and, for file:line
    /// misses, up to 5 nearest lines in that file that DO define vars).
    /// Shared by defuse/pts/aliases and (Task 5.1) vfpath/reachable anchors.
    std::vector<const SVF::SVFVar*>
    resolveVars(const nlohmann::json& spec) const;

    /// Module paths as given to the ctor; reported in schema().program.
    std::vector<std::string> modules;
    SVF::SVFIR* pag = nullptr;
    SVF::AndersenBase* ander = nullptr;
    SVF::SVFGBuilder svfBuilder; // owns the SVFG; must outlive svfg
    SVF::SVFG* svfg = nullptr;
    SVF::CallGraph* callgraph = nullptr;
};
