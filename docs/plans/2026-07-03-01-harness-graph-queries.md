# Plan: svf-harness Graph Query Layer

**Epic:** E1+ / representation layer expansion
**Design:** follows `docs/designs/2026-06-10-svf-harness-thin-slice.md`; this is an incremental graph-browsing expansion, not a precision-configuration change.

## Summary

Add first-class graph browsing queries to `svf-harness` so CLI/MCP users can inspect the major graphs that are already built in the daemon: ICFG, SVFG, SVFIR/PAG, and Andersen's resolved call graph. This deliberately does not add new bitcode modes, precision settings, or separate SVF analyses; those remain deferred so this plan stays testable and cohesive.

**Decisions locked in:**
- Scope is option A from the user decision: SVFG/PAG/ICFG/CallGraph node and edge browsing.
- Precision remains v0: `AndersenWaveDiff + full SVFG`; no `load_program` config changes in this plan.
- Query output stays JSON-only and uses the existing evidence contract wherever an SVF node has an evidence helper.
- MCP remains a thin static wrapper; new query names must be added to the static tool list and drift-guarded against daemon `schema`.

---

## Phase 1: Schema And Drift Guards

Expose the new method names in tests first, so CLI/MCP drift is caught before implementation.

### [x] Task 1.1: Add failing daemon schema tests
- [x] In `svf-llvm/tools/Harness/tests/run_tests.py`, add a test that calls `schema` and expects these implemented methods in order after existing `reachable`: `graphs`, `graph_nodes`, `graph_edges`, `node`, `neighbors`.
- [x] In the same test, assert each new method has non-empty `description`, object `params`, and non-empty `returns`.
- [x] Run:
  ```bash
  env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
    'source ./setup.sh > /dev/null && SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
     python3 svf-llvm/tools/Harness/tests/run_tests.py -v HarnessTest.test_schema_graph_query_methods'
  ```
- [x] Expected before implementation: FAIL because the schema does not list the graph-query methods.

### [x] Task 1.2: Add MCP drift expectation
- [x] In `mcp/svf_harness_mcp/test_smoke.py`, extend `EXPECTED_QUERY_TOOLS` with `graphs`, `graph_nodes`, `graph_edges`, `node`, `neighbors`.
- [x] Keep the existing schema-vs-tool-set assertion so MCP fails until daemon schema and wrapper tools are both updated.

## Phase 2: Core Graph Query Implementation

Implement read-only graph browsing against existing in-memory graph objects.

### [x] Task 2.1: Add QueryEngine API and dispatch entries
- [x] In `svf-llvm/tools/Harness/QueryEngine.h`, declare:
  - `graphs(const json&) const`
  - `graphNodes(const json&) const`
  - `graphEdges(const json&) const`
  - `nodeQ(const json&) const`
  - `neighbors(const json&) const`
- [x] In `svf-llvm/tools/Harness/QueryEngine.cpp`, add method-table entries named `graphs`, `graph_nodes`, `graph_edges`, `node`, `neighbors`.

### [x] Task 2.2: Create graph query helpers
- [x] Create `svf-llvm/tools/Harness/GraphQueries.cpp`.
- [x] Add graph-name parsing for exactly: `icfg`, `svfg`, `svfir`, `callgraph`.
- [x] Add shared parameter handling:
  - `graph` required for all methods except `graphs`
  - `limit` optional, default 100, accepted range 1..1000
  - `offset` optional, default 0
  - `kind` optional node/edge kind filter
  - `func` optional node filter where evidence/location supports it
  - `id` required for `node` and `neighbors`
  - `direction` optional for `neighbors`: `out`, `in`, `both` default `both`
- [x] Update `svf-llvm/tools/Harness/CMakeLists.txt` to compile `GraphQueries.cpp`.

### [x] Task 2.3: Implement `graphs`
- [x] Return graph inventory:
  ```json
  {
    "graphs": [
      {"name":"icfg","nodes":93,"edges":...},
      {"name":"svfg","nodes":223,"edges":...},
      {"name":"svfir","nodes":183,"edges":...},
      {"name":"callgraph","nodes":9,"edges":...}
    ]
  }
  ```
- [x] Edge counts should be computed from out-edges/callgraph edges and be deterministic.

### [x] Task 2.4: Implement `graph_nodes` and `node`
- [x] `graph_nodes` returns `{graph, nodes, total, offset, limit, truncated}` with stable node-id order.
- [x] For `icfg`, use `evidence::node(const ICFGNode*)`.
- [x] For `svfg`, use `evidence::node(const VFGNode*)`.
- [x] For `svfir`, use `evidence::node(const SVFVar*)`.
- [x] For `callgraph`, return custom records with `{kind:"CallGraphNode", id, function, loc, is_decl, num_args}`.
- [x] `node` returns `{graph, node}` for a single id or a clear JSON-RPC error if the id is absent.

### [x] Task 2.5: Implement `graph_edges` and `neighbors`
- [x] `graph_edges` returns `{graph, edges, total, offset, limit, truncated}` with stable `(src,dst,kind)` ordering.
- [x] Edge rows use `{src, dst, kind}` and include `callsite` evidence where cheaply available for call-graph direct/indirect callsites.
- [x] `neighbors` returns `{graph, id, in_edges, out_edges, node}` and honors `direction`.
- [x] Do not build new graph structures or run new analyses.

## Phase 3: Schema, MCP, Docs

Make the new surface discoverable through the same contracts as v0.

### [x] Task 3.1: Update schema registry
- [x] In `svf-llvm/tools/Harness/Schema.cpp`, document all five new methods, accepted graph names, parameters, caps, and return shapes.
- [x] Keep `implemented` flags derived from the live method table.

### [x] Task 3.2: Update MCP static wrapper
- [x] In `mcp/svf_harness_mcp/server.py`, add static tool descriptions for `graphs`, `graph_nodes`, `graph_edges`, `node`, `neighbors`.
- [x] Ensure the wrapper still forwards one generic `params` dict verbatim.

### [x] Task 3.3: Update user-facing docs
- [x] In `svf-llvm/tools/Harness/README.md`, update the method count/list and add one compact graph-query example.
- [x] In `mcp/svf_harness_mcp/README.md`, update the tool count/list.
- [x] Add a short deferred note to `docs/FUTURE.md` that precision configuration and DDA/CFL/SABER/MTA/AE remain after graph browsing.

## Phase 4: Integration Tests And Real Run

Verify through oneshot, daemon CLI, MCP, and example-level behavior.

### [x] Task 4.1: Add focused query tests
- [x] In `svf-llvm/tools/Harness/tests/run_tests.py`, add tests for:
  - `graphs` includes `icfg`, `svfg`, `svfir`, `callgraph`
  - `graph_nodes` on `svfg` with `kind:"LoadVFGNode"` finds demo.c line 11
  - `graph_edges` on `icfg` returns `IntraCFGEdge` rows
  - `node` on an SVFG id round-trips the same evidence
  - `neighbors` on that SVFG id returns at least one incoming or outgoing edge

### [x] Task 4.2: Run verification
- [x] Build:
  ```bash
  env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
    'source ./setup.sh > /dev/null && cmake --build Release-build -j8 --target svf-harness'
  ```
- [x] Run full harness suite:
  ```bash
  env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
    'source ./setup.sh > /dev/null && SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
     python3 svf-llvm/tools/Harness/tests/run_tests.py -v'
  ```
- [x] Run MCP smoke:
  ```bash
  env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
    'source ./setup.sh > /dev/null && SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
     /home/xiao/program/py311-mcp/bin/python mcp/svf_harness_mcp/test_smoke.py -v'
  ```
- [x] Manually start daemon and query `graphs`, `graph_nodes`, `neighbors`, then `shutdown`.

## Verification
- [x] New schema drift test fails before implementation and passes after implementation.
- [x] MCP smoke lists the new tools and verifies them against daemon schema.
- [x] Full harness suite passes.
- [x] Manual daemon run demonstrates graph browsing over a real fixture.
- [x] `docs/PROGRESS.md` updated with results and next steps.

## Results

Completed 2026-07-03.

- Built `svf-harness` successfully with `GraphQueries.cpp`.
- Full harness suite: 39/39 tests passed.
- Standalone MCP smoke: 4/4 tests passed, 18 tools listed and schema-aligned.
- Examples: `svf-llvm/tools/Harness/examples/run_all.sh` passed 5/5 scripts.
- CTest: `harness_integration` and `harness_examples` passed.
- Manual daemon run on `demo.c` exercised `graphs`, `graph_nodes`, and `neighbors`.
