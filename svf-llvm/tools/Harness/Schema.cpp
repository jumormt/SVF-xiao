//===- Schema.cpp -- self-describing schema registry for svf-harness -----===//
//
// Hand-maintained registry of everything an LLM client needs in order to
// interpret svf-harness results and choose the right query method.
//
// node_kinds: enumerated from the toString() class-name prefixes of
// ICFG/VFG/SVFG/SVFVar subclasses (audited in svf/lib/Graphs/ICFG.cpp,
// svf/lib/Graphs/VFG.cpp, svf/lib/Graphs/SVFG.cpp,
// svf/lib/SVFIR/SVFVariables.cpp) — NOT from the GNodeK enums. Evidence.cpp
// derives the "kind" field of every evidence record from the same prefixes,
// so this is the only enumeration that keeps the invariant "every emitted
// kind appears in schema().node_kinds". In particular it includes the
// printed-only aliases that exist in no enum:
//   InterMSSAPHISVFGNode prints FormalINPHISVFGNode / ActualOUTPHISVFGNode
//   InterPHIVFGNode      prints FormalParmPHI       / ActualRetPHI
//
// edge_kinds: the ICFG edge classes and SVFG edge families
// (svf/include/Graphs/ICFGEdge.h, svf/include/Graphs/VFGEdge.h,
// svf/include/Graphs/SVFGEdge.h). These exact names will be used as the
// per-step `edge` labels of vfpath/reachable results (Task 5.1), so a client
// can match path steps against this list.
//
//===----------------------------------------------------------------------===//
#include "Schema.h"

using json = nlohmann::json;

namespace
{

/// One node-kind entry. Every node kind carries the uniform evidence
/// attributes {kind,id,loc,ir} (see "evidence_record" below).
json nodeKind(const char* name, const char* graph, const char* description)
{
    return json{{"name", name},
                {"graph", graph},
                {"description", description},
                {"attributes", json::array({"kind", "id", "loc", "ir"})}};
}

json edgeKind(const char* name, const char* graph, const char* description)
{
    return json{{"name", name},
                {"graph", graph},
                {"description", description}};
}

/// One method parameter: {"type","description","required"}.
json param(const char* type, const char* description, bool required)
{
    return json{{"type", type},
                {"description", description},
                {"required", required}};
}

json method(const char* name, const char* description, json params,
            const char* returns)
{
    return json{{"name", name},
                {"description", description},
                {"params", std::move(params)},
                {"returns", returns}};
}

/// Reusable description of a "variable anchor" object, accepted wherever a
/// method needs to identify a program variable or value (defuse/pts/aliases
/// var, vfpath source/sink).
const char* kAnchorDesc =
    "Variable anchor object. Use ONE of these shapes: "
    "{\"file\": \"demo.c\", \"line\": 7, \"name\": \"b\"?} to pick the value "
    "defined at a source line (name disambiguates when several values are "
    "defined there); {\"func\": \"malloc\", \"ret\": true} for a function's "
    "return value; {\"func\": \"memcpy\", \"arg\": 0} for a call argument.";

json nodeKinds()
{
    json a = json::array();

    // ---- ICFG: interprocedural control-flow graph. One node ~ one LLVM
    // instruction or function boundary; ask `cfg` for these. ----
    a.push_back(nodeKind("ICFGNode", "icfg",
        "Abstract base of all control-flow nodes. Appears only as a fallback; "
        "concrete results use one of the specific ICFG kinds below."));
    a.push_back(nodeKind("GlobalICFGNode", "icfg",
        "Single synthetic node holding all global-scope initializations "
        "(global variables, constants, vtables). Its loc is empty/0 because it "
        "corresponds to no single source line; look at its contained "
        "statements' ir text instead. Relevant when tracking flows that start "
        "from a global initializer."));
    a.push_back(nodeKind("IntraICFGNode", "icfg",
        "One ordinary (non-call) instruction inside a function: load, store, "
        "gep, arithmetic, branch, return instruction, etc. The workhorse node "
        "of `cfg` results; its loc gives the exact source file:line of the "
        "instruction."));
    a.push_back(nodeKind("CallICFGNode", "icfg",
        "A call site: the point where control leaves the caller. Carries the "
        "callee and actual arguments; `callers`/`callees` evidence and the "
        "callsite field of call edges use this kind. Match its loc against "
        "source to see which call expression it is."));
    a.push_back(nodeKind("RetICFGNode", "icfg",
        "The return site paired with a CallICFGNode: where control re-enters "
        "the caller and the call's result (if any) becomes available. A "
        "value 'after the call' lives here, not at the CallICFGNode."));
    a.push_back(nodeKind("FunEntryICFGNode", "icfg",
        "Synthetic entry of a function, before its first instruction; formal "
        "parameters are bound here. Paths that enter a callee pass through "
        "this node. For declared-only (external) functions loc may be "
        "empty."));
    a.push_back(nodeKind("FunExitICFGNode", "icfg",
        "Synthetic exit of a function, after all returns; the function's "
        "return value is collected here before flowing back to each "
        "RetICFGNode. Paths that leave a callee pass through this node."));

    // ---- VFG/SVFG: (sparse) value-flow graph. One node ~ one def or
    // memory-SSA def; vfpath/reachable/defuse traverse these. ----
    a.push_back(nodeKind("VFGNode", "vfg",
        "Abstract base of all value-flow nodes; also printed by internal "
        "helper nodes with no specialized text. Treat as 'some value-flow "
        "step' and rely on loc/ir for detail."));
    a.push_back(nodeKind("StmtVFGNode", "vfg",
        "Base of statement-backed value-flow nodes (load/store/copy/gep/"
        "addr). Concrete results normally use the specific kinds below."));
    a.push_back(nodeKind("AddrVFGNode", "vfg",
        "An address-taken/allocation step: p = &x, or p = malloc(...)/alloca. "
        "This is where an abstract memory object enters the value flow — the "
        "usual SOURCE node of heap bug queries (e.g. the malloc in a "
        "use-after-free)."));
    a.push_back(nodeKind("CopyVFGNode", "vfg",
        "A direct value copy p = q (assignment, cast, bitcast). Propagates a "
        "pointer/value unchanged; in a path it just relays the flow to the "
        "next step."));
    a.push_back(nodeKind("GepVFGNode", "vfg",
        "A field/element address computation p = &q->f or &q[i] "
        "(getelementptr). A flow passing through it now refers to a sub-field "
        "of the original object — important when judging whether two accesses "
        "touch the same field."));
    a.push_back(nodeKind("LoadVFGNode", "vfg",
        "A read through a pointer: p = *q. The typical SINK of use-after-free "
        "/ uninitialized-read queries; its loc is the dereference site."));
    a.push_back(nodeKind("StoreVFGNode", "vfg",
        "A write through a pointer: *p = q. Defines memory; indirect value "
        "flow (Ind edges) starts at stores and ends at loads of the same "
        "abstract object."));
    a.push_back(nodeKind("PHIVFGNode", "vfg",
        "A top-level SSA phi: the value is one of several candidates merged "
        "at a control-flow join (if/else, loop). A path through it means the "
        "flow is conditional on which branch executed."));
    a.push_back(nodeKind("IntraPHIVFGNode", "vfg",
        "An intraprocedural phi (the common concrete case of PHIVFGNode): "
        "merges values of one variable from different branches inside one "
        "function."));
    a.push_back(nodeKind("FormalParmPHI", "vfg",
        "Printed by InterPHIVFGNode when a formal parameter merges incoming "
        "actuals from multiple call sites (no enum kind of its own). Seeing "
        "it in a path means the flow entered the callee from one of several "
        "callers — check the surrounding call edge for which one."));
    a.push_back(nodeKind("ActualRetPHI", "vfg",
        "Printed by InterPHIVFGNode when a call result merges returns from "
        "multiple possible callees, e.g. an indirect call through a function "
        "pointer (no enum kind of its own). The actual callee is ambiguous at "
        "this node."));
    a.push_back(nodeKind("ArgumentVFGNode", "vfg",
        "Base of parameter/return passing nodes; concrete results use the "
        "four Actual/Formal kinds below."));
    a.push_back(nodeKind("ActualParmVFGNode", "vfg",
        "An argument at a call site, on the caller side: the value passed "
        "into the call. Paired with FormalParmVFGNode via a CallDirSVFGEdge; "
        "this is where a flow crosses INTO a callee."));
    a.push_back(nodeKind("FormalParmVFGNode", "vfg",
        "A parameter inside the callee: the value received from a call. The "
        "first node of a flow after it crosses a function boundary "
        "inwards."));
    a.push_back(nodeKind("FormalRetVFGNode", "vfg",
        "The callee-side return value: what a function hands back at its "
        "exit. Paired with ActualRetVFGNode via a RetDirSVFGEdge; this is "
        "where a flow crosses OUT of a callee."));
    a.push_back(nodeKind("ActualRetVFGNode", "vfg",
        "The caller-side call result: the value of `x` in x = f(...). For "
        "source anchors like {func: 'malloc', ret: true}, the flow surfaces "
        "in the caller at this kind of node."));
    a.push_back(nodeKind("NullPtrVFGNode", "vfg",
        "The definition point of a null pointer constant. A flow originating "
        "here means the value may be NULL — relevant for null-dereference "
        "reasoning, noise for most other queries."));
    a.push_back(nodeKind("BranchVFGNode", "vfg",
        "A conditional branch statement viewed as a value-flow node (its "
        "condition uses a value). Appears when tracking how a value "
        "influences control flow, not data."));
    a.push_back(nodeKind("CmpVFGNode", "vfg",
        "A comparison c = (a OP b). The result is a boolean derived from its "
        "operands; flows through it become control-relevant rather than "
        "pointer-relevant."));
    a.push_back(nodeKind("BinaryOPVFGNode", "vfg",
        "A binary arithmetic/bitwise operation r = a OP b. Pointer identity "
        "is generally not preserved through it; for taint-style reasoning the "
        "data still flows."));
    a.push_back(nodeKind("UnaryOPVFGNode", "vfg",
        "A unary operation r = OP a (e.g. fneg). Same caveats as "
        "BinaryOPVFGNode."));

    // ---- SVFG memory-SSA (MSSA) nodes: indirect flow through memory.
    // These have no single source line of their own; their loc derives from
    // the enclosing function/callsite. ----
    a.push_back(nodeKind("MRSVFGNode", "vfg",
        "Base of memory-region (memory-SSA) nodes. A 'memory region' is the "
        "set of abstract objects a pointer may touch; these nodes track "
        "indirect value flow through memory rather than through SSA "
        "registers."));
    a.push_back(nodeKind("FormalINSVFGNode", "vfg",
        "Memory state of a region on ENTRY to a function (callee side): the "
        "memory the function may read as it begins. An indirect flow entering "
        "a callee passes ActualIN (caller) -> FormalIN (callee)."));
    a.push_back(nodeKind("FormalOUTSVFGNode", "vfg",
        "Memory state of a region on EXIT from a function (callee side): the "
        "memory the function may have written. An indirect flow leaving a "
        "callee passes FormalOUT (callee) -> ActualOUT (caller)."));
    a.push_back(nodeKind("ActualINSVFGNode", "vfg",
        "Memory state of a region just BEFORE a call site (caller side): what "
        "memory the call may consume. Its ir text includes the callsite "
        "location (CS[...])."));
    a.push_back(nodeKind("ActualOUTSVFGNode", "vfg",
        "Memory state of a region just AFTER a call site (caller side): what "
        "memory the call may have updated. A store inside the callee becomes "
        "visible to the caller through this node."));
    a.push_back(nodeKind("MSSAPHISVFGNode", "vfg",
        "Base of memory-SSA phi nodes: the memory state of a region merged "
        "from several predecessors. Indicates the indirect flow is "
        "path-sensitive at this point."));
    a.push_back(nodeKind("IntraMSSAPHISVFGNode", "vfg",
        "Memory-SSA phi at a control-flow join inside one function: memory "
        "written on different branches merges here before a later load can "
        "observe it."));
    a.push_back(nodeKind("FormalINPHISVFGNode", "vfg",
        "Printed by InterMSSAPHISVFGNode at a function entry (no enum kind of "
        "its own): the entry memory state merged from MULTIPLE call sites. A "
        "path through it means the observed memory may come from any of "
        "several callers."));
    a.push_back(nodeKind("ActualOUTPHISVFGNode", "vfg",
        "Printed by InterMSSAPHISVFGNode after a call site (no enum kind of "
        "its own): the post-call memory state merged from MULTIPLE possible "
        "callees (e.g. an indirect call). The writing callee is ambiguous at "
        "this node."));

    // ---- SVFIR (PAG) variables: abstract values and memory objects.
    // pts/aliases results are made of these; ObjVar-family entries in a pts
    // set are the abstract objects a pointer may point to. ----
    a.push_back(nodeKind("SVFVar", "svfir",
        "Abstract base of all SVFIR variables. Concrete results use the "
        "specific kinds below; the key split is ValVar-family (pointers/"
        "values) vs ObjVar-family (abstract memory objects)."));
    a.push_back(nodeKind("ValVar", "svfir",
        "A top-level SSA value: a pointer or scalar held in a register, e.g. "
        "a local `char* b`. `pts` queries take a ValVar and return the "
        "ObjVars it may point to."));
    a.push_back(nodeKind("ObjVar", "svfir",
        "An abstract memory object (allocation site): something a pointer "
        "can point to. Each entry in a points-to set is an ObjVar whose loc "
        "is the allocation's source line."));
    a.push_back(nodeKind("ArgValVar", "svfir",
        "A formal parameter value of a function. Useful as an aliases/defuse "
        "anchor when asking 'who else sees the buffer passed in as p?'."));
    a.push_back(nodeKind("GepValVar", "svfir",
        "A pointer INTO a field/element of an object: the result of &p->f or "
        "&p[i]. Distinguishes field pointers from the base pointer when "
        "comparing aliases."));
    a.push_back(nodeKind("GepObjVar", "svfir",
        "A field of an abstract object (object + constant offset). A pts set "
        "containing GepObjVar means the pointer targets a specific field, "
        "not the whole object."));
    a.push_back(nodeKind("BaseObjVar", "svfir",
        "A whole abstract object treated field-insensitively (the base of "
        "any GepObjVar derived from it). Generic kind for objects with no "
        "more specific class."));
    a.push_back(nodeKind("HeapObjVar", "svfir",
        "A heap allocation site: the object created by malloc/new at the loc "
        "line. The central object of use-after-free/leak queries — its loc "
        "tells you WHICH malloc."));
    a.push_back(nodeKind("StackObjVar", "svfir",
        "A stack allocation: a local variable or alloca. Seeing it in a pts "
        "set rules heap bugs out for that target but enables "
        "escape-of-stack-address reasoning."));
    a.push_back(nodeKind("GlobalObjVar", "svfir",
        "A global variable's memory object. Flows into/out of it persist "
        "across calls — shared state."));
    a.push_back(nodeKind("GlobalValVar", "svfir",
        "The value/address of a global symbol as used in code (the pointer "
        "side of GlobalObjVar)."));
    a.push_back(nodeKind("FunValVar", "svfir",
        "The value of a function symbol: a function address being passed or "
        "stored. Its presence in a pts set of a called pointer tells you the "
        "possible indirect-call target."));
    a.push_back(nodeKind("FunObjVar", "svfir",
        "The function itself as an abstract object (also serves as SVF's "
        "function handle; `functions` results describe these). pts(fp) "
        "containing a FunObjVar names a callee of the function pointer."));
    a.push_back(nodeKind("RetValPN", "svfir",
        "The unique return-value variable of a function: all its `return e` "
        "statements merge here. Anchor {func: F, ret: true} resolves to this "
        "(or to the caller-side result)."));
    a.push_back(nodeKind("VarArgValPN", "svfir",
        "The unique variadic-argument variable of a varargs function: all "
        "`...` arguments merge here, so flows through it are coarse."));
    a.push_back(nodeKind("ConstAggValVar", "svfir",
        "A constant aggregate value (struct/array literal). Usually noise "
        "for pointer queries unless tracking data embedded in constants."));
    a.push_back(nodeKind("ConstDataValVar", "svfir",
        "A constant non-pointer datum (generic). Cannot point to memory; "
        "safe to ignore in alias reasoning."));
    a.push_back(nodeKind("ConstFPValVar", "svfir",
        "A floating-point constant value. Ignorable for pointer analysis."));
    a.push_back(nodeKind("ConstIntValVar", "svfir",
        "An integer constant value. Ignorable for pointer analysis unless "
        "cast to a pointer."));
    a.push_back(nodeKind("ConstNullPtrValVar", "svfir",
        "The NULL pointer constant. A pts set containing its object means "
        "the pointer may be NULL."));
    a.push_back(nodeKind("ConstAggObjVar", "svfir",
        "The memory object of a constant aggregate (e.g. a string table in "
        "rodata)."));
    a.push_back(nodeKind("ConstDataObjVar", "svfir",
        "The memory object of constant data (generic read-only datum)."));
    a.push_back(nodeKind("ConstFPObjVar", "svfir",
        "The object form of a floating-point constant."));
    a.push_back(nodeKind("ConstIntObjVar", "svfir",
        "The object form of an integer constant."));
    a.push_back(nodeKind("ConstNullPtrObjVar", "svfir",
        "The abstract 'null object'. Pointers whose pts set holds this may "
        "be NULL at dereference time."));
    a.push_back(nodeKind("DummyValVar", "svfir",
        "An internal placeholder value with no source counterpart (loc is "
        "empty/0). Bridges modeling gaps; do not map it to source."));
    a.push_back(nodeKind("DummyObjVar", "svfir",
        "An internal placeholder object, e.g. the unknown/black-hole object "
        "for unmodeled memory. In a pts set it means 'could point anywhere "
        "we cannot see' — treat the result as imprecise."));
    a.push_back(nodeKind("IntrinsicValVar", "svfir",
        "The value of an LLVM intrinsic (llvm.dbg.*, llvm.memcpy handles "
        "etc.). Usually analysis plumbing, not user logic."));
    a.push_back(nodeKind("BasicBlockValVar", "svfir",
        "A basic-block address value (blockaddress / computed goto target). "
        "Rare; only relevant for indirect-branch reasoning."));
    a.push_back(nodeKind("AsmPCValVar", "svfir",
        "A program-counter-like value from inline assembly. Opaque to the "
        "analysis; flows through it are unmodeled."));
    return a;
}

json edgeKinds()
{
    json a = json::array();
    // ICFG edges (svf/include/Graphs/ICFGEdge.h).
    a.push_back(edgeKind("IntraCFGEdge", "icfg",
        "Control flow between two nodes inside the same function. May carry "
        "a branch condition + successor value when it leaves a conditional "
        "branch — that tells you which way the branch went."));
    a.push_back(edgeKind("CallCFGEdge", "icfg",
        "Control flow from a CallICFGNode into a callee's FunEntryICFGNode. "
        "Crossing one means the path enters another function; the source "
        "node is the callsite evidence."));
    a.push_back(edgeKind("RetCFGEdge", "icfg",
        "Control flow from a callee's FunExitICFGNode back to the caller's "
        "RetICFGNode. Crossing one means the path returns from a call; the "
        "destination node identifies the callsite."));
    // SVFG edge families (svf/include/Graphs/VFGEdge.h, SVFGEdge.h). vfpath/
    // reachable steps will label their `edge` field with these names
    // (Task 5.1): Dir = via SSA def-use, Ind = via memory (store->load).
    a.push_back(edgeKind("IntraDirSVFGEdge", "vfg",
        "Direct def-use flow inside one function: the destination uses the "
        "value the source defines, register-to-register. The most precise "
        "kind of value-flow step."));
    a.push_back(edgeKind("CallDirSVFGEdge", "vfg",
        "Direct flow of an argument into a callee's parameter "
        "(ActualParm -> FormalParm), tagged with a callsite id. In a path, "
        "this is where the value enters another function."));
    a.push_back(edgeKind("RetDirSVFGEdge", "vfg",
        "Direct flow of a return value back to the call result "
        "(FormalRet -> ActualRet), tagged with a callsite id. In a path, "
        "this is where the value leaves a function."));
    a.push_back(edgeKind("IntraIndSVFGEdge", "vfg",
        "Indirect flow through memory inside one function: a store defines "
        "an abstract object that a later load (or memory-SSA node) reads. "
        "Precision depends on the pointer analysis — the store and load may "
        "only MAY-alias."));
    a.push_back(edgeKind("CallIndSVFGEdge", "vfg",
        "Indirect memory flow into a call: memory state before the callsite "
        "feeds the callee's entry (ActualIN -> FormalIN), tagged with a "
        "callsite id. Means the callee may read memory the caller wrote."));
    a.push_back(edgeKind("RetIndSVFGEdge", "vfg",
        "Indirect memory flow out of a call: the callee's exit memory feeds "
        "the state after the callsite (FormalOUT -> ActualOUT), tagged with "
        "a callsite id. Means the caller may observe memory the callee "
        "wrote."));
    a.push_back(edgeKind("ThreadMHPIndSVFGEdge", "vfg",
        "Indirect memory flow between statements that may happen in parallel "
        "in different threads. Only present when thread analysis runs; "
        "signals a potential cross-thread store->load (data-race style) "
        "flow."));
    return a;
}

json methods()
{
    json a = json::array();
    a.push_back(method("schema",
        "Return this self-describing schema: every node/edge kind with "
        "explanations, every method with parameters, the evidence record "
        "shape, and the loaded program's identity. Call it first in a "
        "session to learn what you can ask.",
        json::object(),
        "this document: {node_kinds, edge_kinds, methods, evidence_record, program}"));
    a.push_back(method("summary",
        "Size of the loaded program's analysis graphs. Cheap sanity check "
        "that the right program is loaded and a scale hint before issuing "
        "broad queries.",
        json::object(),
        "{functions, icfg_nodes, svfg_nodes, pag_nodes} counts"));
    a.push_back(method("functions",
        "List functions by name pattern. Use it to resolve the exact symbol "
        "names (and whether they are definitions or external declarations) "
        "before calling callers/callees/cfg.",
        json{{"pattern", param("string",
            "ECMAScript regex matched anywhere in the function name "
            "(search semantics). Omit or empty = all functions.", false)}},
        "{functions: [{name, loc, is_decl, num_args}], total, truncated} "
        "(capped at 200, sorted by name)"));
    a.push_back(method("callers",
        "Who calls this function? Each result is one call edge with callsite "
        "evidence (file:line of the call) and whether the call is direct or "
        "resolved through a function pointer by the pointer analysis. When "
        "multiple static functions share the same name (across translation "
        "units), results from ALL matching nodes are merged and "
        "matched_functions reports how many were found.",
        json{{"func", param("string",
            "Exact function name (resolve with `functions` first).", true)}},
        "{function, calls: [{caller, callee, callsite: <evidence node>, "
        "direct: bool}], total, truncated, matched_functions: N}"));
    a.push_back(method("callees",
        "What does this function call? Includes indirect calls resolved by "
        "Andersen's analysis, so a function-pointer call reports its "
        "possible concrete targets. When multiple static functions share the "
        "same name (across translation units), results from ALL matching "
        "nodes are merged and matched_functions reports how many were found.",
        json{{"func", param("string",
            "Exact function name (resolve with `functions` first).", true)}},
        "{function, calls: [{caller, callee, callsite: <evidence node>, "
        "direct: bool}], total, truncated, matched_functions: N}"));
    a.push_back(method("cfg",
        "The control-flow graph of one function: its ICFG nodes (one per "
        "instruction/boundary, with source lines) and intraprocedural edges. "
        "Use it to understand statement order and branching before "
        "interpreting a value-flow path.",
        json{{"func", param("string",
            "Exact function name (resolve with `functions` first).", true)}},
        "{nodes: [<evidence node>], edges: [{src, dst, kind}]}"));
    a.push_back(method("defuse",
        "Definition and use sites of one variable: where it gets its value "
        "and every statement that consumes it. Lighter than vfpath when you "
        "only need 'where is b used after this line?'.",
        json{{"var", param("object", kAnchorDesc, true)}},
        "{var: <evidence node>, defs: [<evidence node>], uses: [<evidence node>]}"));
    a.push_back(method("pts",
        "Andersen's may-points-to set of a pointer: the abstract objects "
        "(allocation sites) it can target. Answers 'which malloc/global/"
        "stack slot does p point to?'. Over-approximate: targets MAY be "
        "pointed to.",
        json{{"var", param("object", kAnchorDesc, true)}},
        "{var: <evidence node>, pts: [<evidence node>]} (objects are "
        "ObjVar-family kinds)"));
    a.push_back(method("aliases",
        "Other pointers that may alias this one (overlapping points-to "
        "sets). Use it to find every name a buffer is reachable through "
        "before concluding a write cannot affect it. May-alias, capped, "
        "see `truncated`.",
        json{{"var", param("object", kAnchorDesc, true)}},
        "{var: <evidence node>, aliases: [<evidence node>], truncated}"));
    a.push_back(method("vfpath",
        "Value-flow paths from a source to a sink over the sparse value-flow "
        "graph: HOW a value gets from A to B, step by step, with evidence "
        "per step. The core method for taint/use-after-free style questions "
        "(e.g. source = malloc return, sink = a dereference line). Each "
        "step's `edge` label is one of edge_kinds.",
        json{{"source", param("object", kAnchorDesc, true)},
             {"sink", param("object",
                 "Same anchor shapes as `source`; for sinks a bare "
                 "{file, line} matches any value-flow node at that line.",
                 true)},
             {"k", param("integer",
                 "Max number of distinct paths to return (default 3).",
                 false)},
             {"max_visited", param("integer",
                 "Search budget: max SVFG nodes to visit before giving up "
                 "(default 100000). Raise for large programs.", false)}},
        "{paths: [{steps: [{node: <evidence>, edge, callsite?}]}], truncated}"));
    a.push_back(method("reachable",
        "Boolean value-flow reachability from one source to MANY sinks at "
        "once (each with a witness path if reachable). Cheaper than calling "
        "vfpath per sink when screening candidate sinks.",
        json{{"source", param("object", kAnchorDesc, true)},
             {"sinks", param("array",
                 "Array of sink anchors (same shapes as vfpath's sink).",
                 true)},
             {"max_visited", param("integer",
                 "Search budget shared across sinks (default 100000).",
                 false)}},
        "[{sink, reachable: bool, first_path?}]"));
    return a;
}

json evidenceRecord()
{
    return json{
        {"description",
         "Uniform record attached to every graph node a method returns. "
         "Always check `loc` to map a result to source, and `kind` against "
         "node_kinds to interpret what the node means."},
        {"fields", json{
            {"kind", "Node class name; one of node_kinds[].name."},
            {"id", "Stable numeric node id within this daemon's loaded "
                   "program. Valid only for this program instance — do not "
                   "persist across reloads."},
            {"loc", json{
                {"file", "Source file path; \"\" when the node has no source "
                         "counterpart (declarations, globals, synthetic and "
                         "memory-SSA nodes)."},
                {"line", "1-based source line; 0 when unknown (same cases as "
                         "empty file)."},
                {"func", "Enclosing function name; \"\" for global-scope "
                         "nodes."}}},
            {"ir", "The node's SVF textual dump (class name + statement "
                   "text). Truncated to ~200 bytes with a trailing … "
                   "(horizontal ellipsis) when longer; truncation never "
                   "splits a UTF-8 codepoint."}}}};
}

} // namespace

json schema::registry()
{
    return json{{"node_kinds", nodeKinds()},
                {"edge_kinds", edgeKinds()},
                {"methods", methods()},
                {"evidence_record", evidenceRecord()}};
}
