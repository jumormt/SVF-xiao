# Design: svf-harness — LLM-Friendly Analysis Harness, Thin Slice (v0)

**Date:** 2026-06-10
**Status:** Approved
**Context:** First concrete step of the DECRA proposal "LLM-Powered Codebase Reasoning
via Static Analysis Tool Synthesis" (~/DECRA_Xiao.pdf). This thin slice cuts one minimal
end-to-end path through the proposal's three layers so an LLM can query SVF's program
graphs and receive structured evidence. It is the v0 skeleton that Task 1 (precision
control), Task 2 (query language + evidence schema), and Task 3 (tool synthesis) will
later grow on.

## Goals

- An LLM (via MCP) or a human (via CLI) can: discover what is queryable (self-describing
  schema), navigate program graphs (call graph, ICFG, def-use, points-to), and ask for
  value-flow paths between a source and a sink with witness evidence.
- Analysis state (SVFIR, Andersen, SVFG) is built once and reused across queries.

## Non-Goals (deferred)

- Declarative query language L_Q (v0 is a fixed method set; the JSON-RPC protocol is the
  future host for L_Q).
- Precision configuration (v0 pins Andersen/AndersenWaveDiff); path conditions and
  abstract-state traces in evidence; incremental graph construction; concurrent query
  handling; Windows support.

## Decisions (from brainstorming)

1. Scope: minimal end-to-end slice rather than starting with proposal Task 1 or 2 alone.
2. Interface: CLI is the core; MCP is a thin wrapper over the same protocol.
3. Capabilities: schema introspection + graph navigation + value-flow paths w/ evidence.
4. State model: resident daemon (clangd-style) + lightweight CLI client over Unix socket.
5. Implementation: approach A — pure C++ in-tree tool using native SVF APIs; rejected
   "C++ dump + Python brain" (serialization wall, duplicated graph logic) and pybind11
   bindings (maintenance tax against fast-moving upstream).

## Architecture

```
svf-llvm/tools/Harness/          # C++ core (one binary: svf-harness)
  svf-harness.cpp                # main: serve mode + client mode
  HarnessServer.{h,cpp}          # Unix socket JSON-RPC loop
  QueryEngine.{h,cpp}            # query primitives (holds SVFIR/SVFG/PTA)
  Evidence.{h,cpp}               # structured evidence records
  Schema.{h,cpp}                 # self-describing schema registry
  external/nlohmann/json.hpp     # vendored single-header JSON
mcp/svf_harness_mcp/             # Python MCP thin wrapper (repo root)
  server.py                      # MCP SDK (stdio); tools forwarded to socket
```

Bootstrap chain (per current SVF master): `LLVMModuleSet::buildSVFModule` →
`SVFIRBuilder::build` → `AndersenWaveDiff::createAndersenWaveDiff` →
`SVFGBuilder::buildFullSVFG`.

## Daemon Lifecycle & Protocol

- `svf-harness serve a.bc [b.bc ...] [--socket PATH]` builds all graphs once, then runs
  an accept loop. Default socket path is derived from a hash of the bitcode paths so
  clients can auto-discover it.
- Protocol: newline-delimited JSON-RPC 2.0 over the Unix socket. Single-threaded request
  handling (SVF structures are not thread-safe).
- Client mode: `svf-harness <method> [args] [--socket PATH]` connects, sends one
  request, prints the JSON response, exits. JSON is the only output format.
- `svf-harness shutdown` terminates the daemon gracefully.

## Query Primitives (v0 surface, 11 RPC methods in 10 groups)

Schema introspection:
1. `schema()` — full registry of node/edge kinds (ICFGNode subclasses, SVFGNode
   subclasses, SVFStmt kinds), their attributes, admissible traversals, each with a
   natural-language description. The LLM's entry point.
2. `summary()` — loaded program overview: modules, #functions, ICFG/SVFG sizes, entry.

Graph navigation:
3. `functions(pattern)` — list functions by name/regex with source loc and signature.
4. `callers(func)` / 5. `callees(callsite|func)` — call-graph both ways, including
   indirect calls resolved by Andersen.
6. `cfg(func)` — ICFG nodes + edges of a function, anchored to source lines.
7. `defuse(var)` — def/use chain of an SVFVar at SVFStmt level.
8. `pts(var)` / `aliases(var)` — points-to set / alias set.

Value flow:
9. `vfpath(source, sink, k?)` — up to k witness paths source→sink on the SVFG (BFS with
   a visited-node budget).
10. `reachable(source, sinks[])` — batch yes/no + first path; cheap triage for the LLM.

Source/sink designation supports both forms an LLM can realistically produce: function
anchors (e.g. return value of `malloc`, i-th argument of `memcpy`) and `file:line`
resolved to SVFVars.

## Evidence Format

Every node in any result carries a uniform record:

```json
{"node": {"kind": "SVFGNode/LoadVFGNode", "id": 1234,
          "loc": {"file": "x.c", "line": 42, "func": "foo"},
          "ir": "load i8* %p"}}
```

`vfpath` evidence = ordered node list + per-step edge kind (intra/call/ret; call edges
carry the callsite anchor). This is the v0 seed of the proposal Task 2.3 value-flow
witness. Every response includes `"truncated": bool` plus graph-size hints so the LLM
knows whether results are complete.

## Error Handling

- All failures are JSON-RPC errors with a structured `hint` field (e.g. unknown function
  → nearest-name candidates) — the minimal form of the proposal's repair hints.
- Bad bitcode: daemon fails fast at startup with the error on stderr/CLI.
- Query explosion: hard visited-node budget; over-budget returns partial results with
  `truncated: true`.

## Testing

- ctest-style integration tests driven by shell/python against Test-Suite's existing
  `.bc` cases: start daemon → run query batch → assert key JSON fields.
- Acceptance demo: on a malloc→free→use test program, replay an LLM workflow
  (schema → functions → vfpath) and obtain a source-located value-flow path.
- MCP layer: 2–3 smoke tests with the MCP SDK in-memory client.

## Risks

- SVF upstream churn: keep the Harness tool's SVF surface narrow (builder chain + graph
  iterators) to make upstream merges cheap.
- Large-program build latency lives entirely in `serve` startup by design; acceptable
  for v0, incremental construction is a later task.
