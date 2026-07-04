//===- QueryEngine.h -- SVF analysis bootstrap + query dispatch ----------===//
#pragma once
#include "MSSA/SVFGBuilder.h"
#include "nlohmann/json.hpp"
#include <memory>
#include <string>
#include <vector>

namespace SVF
{
class SVFIR;
class SVFG;
class AndersenBase;
class AbstractInterpretation;
class CFLAlias;
class CallGraph;
class CallGraphNode;
class DDAClient;
class FlowDDA;
class LeakChecker;
class MTA;
class SVFVar;
class VFGNode;
}

class QueryEngine
{
public:
    struct HarnessConfig
    {
        std::string pointerAnalysis;
        std::string svfgMode;
        bool svfgIndirectCalls;
        bool svfgPostOpts;

        HarnessConfig();
        static HarnessConfig fromJson(const nlohmann::json& j);
        nlohmann::json toJson() const;
    };

    /// Builds LLVM modules -> SVFIR -> Andersen -> SVFG. Throws std::runtime_error on bad input.
    explicit QueryEngine(const std::vector<std::string>& moduleNames);
    QueryEngine(const std::vector<std::string>& moduleNames,
                const HarnessConfig& config);
    ~QueryEngine();
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
    /// CFLAlias may-points-to set of each var resolved from params["var"].
    /// Same output shape as pts(), with analysis="cfl-alias".
    nlohmann::json cflPts(const nlohmann::json& params) const;
    /// CFLAlias may-aliases of each var resolved from params["var"]. Same
    /// same-function candidate scope as aliases(), with analysis="cfl-alias".
    nlohmann::json cflAliases(const nlohmann::json& params) const;
    /// FlowDDA may-points-to set of each var resolved from params["var"].
    /// Same output shape as pts(), with analysis="flowdda".
    nlohmann::json ddaPts(const nlohmann::json& params) const;
    /// FlowDDA may-aliases of each var resolved from params["var"]. Same
    /// same-function candidate scope as aliases(), with analysis="flowdda".
    nlohmann::json ddaAliases(const nlohmann::json& params) const;
    /// SABER memory leak checker summary.
    nlohmann::json saberLeaks(const nlohmann::json& params) const;
    /// SABER double-free checker summary.
    nlohmann::json saberDoubleFrees(const nlohmann::json& params) const;
    /// SABER file open/close leak checker summary.
    nlohmann::json saberFileLeaks(const nlohmann::json& params) const;
    /// MTA thread creation / MHP summary.
    nlohmann::json mtaSummary(const nlohmann::json& params) const;
    /// MTA may-happen-in-parallel query between two source-location anchors.
    nlohmann::json mtaMHP(const nlohmann::json& params) const;
    /// Abstract Execution trace coverage and state-size summary.
    nlohmann::json aeSummary(const nlohmann::json& params) const;
    /// Abstract Execution state at source-location anchors.
    nlohmann::json aeState(const nlohmann::json& params) const;
    /// Value-flow paths from params["source"] to params["sink"] (anchors,
    /// see resolveVars/resolveSinkNodes) over the SVFG: one multi-source BFS
    /// with a parent tree, up to k (default 1, max 10) witness paths — at
    /// most ONE per distinct sink SVFG node, shortest by edge count. Budget
    /// params["max_visited"] (default 100000) caps dequeued nodes; on
    /// exhaustion truncated=true. Bodies live in VFPath.cpp.
    nlohmann::json vfpath(const nlohmann::json& params) const;
    /// Batch reachability: one BFS from params["source"] to each of
    /// params["sinks"] (array, capped at 20), returning per sink
    /// {sink, reachable, first_path?}. Bodies live in VFPath.cpp.
    nlohmann::json reachable(const nlohmann::json& params) const;
    /// Inventory of graph surfaces currently loaded by the daemon.
    nlohmann::json graphs(const nlohmann::json& params) const;
    /// Generic node listing for icfg/svfg/svfir/callgraph.
    nlohmann::json graphNodes(const nlohmann::json& params) const;
    /// Generic edge listing for icfg/svfg/svfir/callgraph.
    nlohmann::json graphEdges(const nlohmann::json& params) const;
    /// Fetch one graph node by graph + id.
    nlohmann::json nodeQ(const nlohmann::json& params) const;
    /// Fetch incoming/outgoing edges around one graph node.
    nlohmann::json neighbors(const nlohmann::json& params) const;
    /// Active analysis configuration plus supported/planned precision surfaces.
    nlohmann::json analysisConfig(const nlohmann::json& params) const;

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
    /// vfpath/reachable SOURCE anchor -> SVFG def nodes: resolveVars(), then
    /// each top-level ValVar's definition SVFG node (hasDefSVFGNode guard).
    /// Throws when no resolved var has a def node. Sorted by node id.
    std::vector<const SVF::VFGNode*>
    resolveSourceNodes(const nlohmann::json& spec) const;
    /// vfpath/reachable SINK anchor -> SVFG nodes. {file, line[, name]}:
    /// every SVFG node attached to an ICFG node at that line (any value-flow
    /// use or def there; name filters by top-level value name). {func,
    /// ret/arg}: the resolved vars' def nodes plus every SVFG node whose
    /// top-level value is one of the vars. Empty resolution throws with the
    /// accepted forms (and nearest value-flow lines for file:line misses).
    std::vector<const SVF::VFGNode*>
    resolveSinkNodes(const nlohmann::json& spec) const;
    /// Lazily construct and solve CFLAlias on first CFL-backed query.
    SVF::CFLAlias* getCFLAlias() const;
    /// Lazily construct FlowDDA on first DDA-backed query. DDAClient must
    /// outlive FlowDDA because the analysis stores a raw client pointer.
    SVF::FlowDDA* getFlowDDA() const;
    /// Run one SABER checker and cache its JSON summary for this daemon.
    nlohmann::json runSaberChecker(const std::string& checker) const;
    /// Lazily run SVF's MTA analysis. MTA writes dot/progress output
    /// internally, so the implementation isolates those side effects.
    SVF::MTA* getMTA() const;
    /// Lazily run SVF's Abstract Execution. The AE engine is a process-wide
    /// singleton; the harness daemon runs one program per process.
    SVF::AbstractInterpretation* getAE() const;

    /// Module paths as given to the ctor; reported in schema().program.
    std::vector<std::string> modules;
    HarnessConfig config;
    SVF::SVFIR* pag = nullptr;
    SVF::AndersenBase* ander = nullptr;
    std::unique_ptr<SVF::SVFGBuilder> svfBuilder; // owns the SVFG; must outlive svfg
    SVF::SVFG* svfg = nullptr;
    SVF::CallGraph* callgraph = nullptr;
    mutable std::unique_ptr<SVF::CFLAlias> cflAlias;
    mutable std::unique_ptr<SVF::DDAClient> ddaClient;
    mutable std::unique_ptr<SVF::FlowDDA> flowDDA;
    mutable bool saberLeaksReady = false;
    mutable bool saberDoubleFreesReady = false;
    mutable bool saberFileLeaksReady = false;
    mutable nlohmann::json saberLeaksCache;
    mutable nlohmann::json saberDoubleFreesCache;
    mutable nlohmann::json saberFileLeaksCache;
    mutable std::unique_ptr<SVF::MTA> mta;
    mutable SVF::AbstractInterpretation* ae = nullptr;
    mutable bool aeReady = false;
};
