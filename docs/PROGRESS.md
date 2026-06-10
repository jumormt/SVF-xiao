# SVF-xiao — LLM-Friendly Program Analysis Harness

Transforming this SVF fork into the harness envisioned by the DECRA proposal
"LLM-Powered Codebase Reasoning via Static Analysis Tool Synthesis" (~/DECRA_Xiao.pdf):
configuration layer (composable precision), representation layer (queryable code
knowledge base), synthesis layer (NL-driven tool synthesis).

## Current Epic
**E1: svf-harness thin slice (v0)** — one minimal end-to-end path: daemon + CLI + MCP
through which an LLM can introspect the schema, navigate SVF's graphs, and get
value-flow paths with structured evidence.

## Epics
- [x] E1: svf-harness thin slice (v0) — daemon/CLI/MCP, 11 query methods, evidence v0
- [ ] E2: declarative query language L_Q (proposal Task 2.2) — future
- [ ] E3: composable precision configuration (proposal Task 1) — future
- [ ] E4: evidence schema with path conditions / abstract traces (proposal Task 2.3) — future

## Plans Index (active/recent)
| Date | Plan | Epic | Status | Notes |
|------|------|------|--------|-------|
| 2026-06-10 | tutorials | E1+ | in-progress | Design: `docs/designs/2026-06-10-tutorials.md`. Plan: `docs/plans/2026-06-10-02-tutorials.md`. Phases 1-2 done (scripts 01-05 + run_all 5/5 PASS; tutorials 01-06 from real runs incl. MCP transcript + mcp-sample.mcp.json). Phase 3 (ctest harness_examples + docs index) remains. |
| 2026-06-10 | svf-harness-thin-slice | E1 | done | **All 7 phases done, 2026-06-10.** 11 methods, daemon+CLI+MCP, 33 py tests, full regression 2267/2267, demo green. Summary: `docs/summaries/2026-06-10-svf-harness-thin-slice.md` |

## Next Steps
- **tutorials Phase 3 Task 3.1:** CMakeLists `harness_examples` ctest running
  examples/run_all.sh (BUILD_TESTING-gated, env like harness_integration,
  RUN_BIG unset); verify `ctest -R harness_examples` serial from Release-build;
  docs/tutorials/README.md index (link all 6 tutorials) + Harness README link;
  full py suite re-check; then plan completion summary (`/ldd-summary`).
- Then: E2 declarative query language L_Q (proposal Task 2.2).

## Known Issues
- Test-Suite must run SERIALLY (`ctest` without `-j`): parallel runs corrupt shared
  generated `.pre.svf.bc` files → ~106 spurious diff_tests-wr-ander segfaults.
- Prebuilt LLVM 21 tarballs need glibc ≥ 2.34; this focal (2.31) machine uses
  conda-forge LLVM via `llvm-21.1.0.obj` symlink → `/home/xiao/program/llvm-21.1.0-conda`.
- Shell env vars `LLVM_DIR`/`Z3_DIR`/`SVF_DIR` point at stale installs; build/test with
  `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR`.

## Key Decisions
| Category | Decision |
|----------|----------|
| Architecture | C++ in-tree tool (`svf-llvm/tools/Harness`), daemon + light CLI over Unix socket JSON-RPC; MCP = thin Python wrapper |
| Precision | v0 pins AndersenWaveDiff + full SVFG; precision config deferred to E3 |
| Output | JSON only; every node carries kind/id/loc/ir evidence record |
| Testing | ctest-style integration tests on Test-Suite .bc cases; serial ctest only |
| JSON lib | vendored nlohmann/json single header |

## Session Log

### 2026-06-10
- **Focus:** upstream sync + toolchain bring-up + harness design
- **Completed:** merged upstream/master (508 commits, d744f6de); fixed focal build via
  conda LLVM 21.1.0; Test-Suite 2266/2266 (serial); pushed focal. Brainstormed +
  approved thin-slice design (`docs/designs/2026-06-10-svf-harness-thin-slice.md`);
  bootstrapped LDD.
- **Tests:** full Test-Suite green (serial)
- **Files:** Dockerfile conflict kept local; docs/ created
- **Blockers:** none

### 2026-06-10 (Task 2.1)
- **Focus:** svf-harness Phase 2 Task 2.1 — QueryEngine + --oneshot summary
- **Completed:** QueryEngine (SVFIR→AndersenWaveDiff→SVFG bootstrap, summary(),
  dispatch()); `--oneshot <method> <bitcode...>` mode with pure-JSON stdout;
  run_tests.py CLANG guard (review carry-over). Commit a94ed8dc.
- **Tests:** 3/3 harness python tests green (test_help, test_fixture_compiles,
  test_oneshot_summary)
- **Files:** svf-llvm/tools/Harness/{QueryEngine.h,QueryEngine.cpp,svf-harness.cpp,
  CMakeLists.txt,tests/run_tests.py}
- **Blockers:** none

### 2026-06-10 (Task 2.2)
- **Focus:** svf-harness Phase 2 Task 2.2 — functions(pattern) + Evidence records
- **Completed:** Evidence.{h,cpp} (uniform kind/id/loc/ir node records; tolerant
  getSourceLoc() parser handling "fl"/"file" key variants); QueryEngine::functions
  (regex search, sorted, 200-cap + truncated flag); `--oneshot --params <json>`
  (stripped before SVF option parsing). Commit cebb73e3.
- **Tests:** 5/5 harness python tests green (new: test_functions_lists_fixture_funcs);
  manual: pattern "make" → make_buf only; bad JSON / bad regex → JSON error exit 1
- **Files:** svf-llvm/tools/Harness/{Evidence.h,Evidence.cpp,QueryEngine.h,
  QueryEngine.cpp,svf-harness.cpp,CMakeLists.txt,tests/run_tests.py}
- **Blockers:** none

### 2026-06-10 (Task 3.1)
- **Focus:** svf-harness Phase 3 Task 3.1 — daemon serve loop + JSON-RPC client mode
- **Completed:** HarnessServer (AF_UNIX newline-delimited JSON-RPC 2.0, stale-socket
  probe, 1 MiB cap, -32700/-32600/-32601+hint/-32000, shutdown method, SIGINT/SIGTERM
  cleanup); `serve` subcommand with `--socket > SVF_HARNESS_SOCKET > FNV-1a default`
  resolution; client mode for any non-serve subcommand (unique /tmp glob fallback);
  QueryEngine method table + methodNames(); shared JsonUtil.h dumpJson. Commit 227e72ee.
- **Tests:** 9/9 harness python tests green (new: test_daemon_roundtrip,
  test_cli_client_subcommand); manual: stale socket reuse, double-daemon error rc=1,
  SIGTERM unlink, oversize -32600, early-disconnect survival, env-var + auto-discovery
- **Files:** svf-llvm/tools/Harness/{HarnessServer.h,HarnessServer.cpp,JsonUtil.h,
  QueryEngine.h,QueryEngine.cpp,svf-harness.cpp,CMakeLists.txt,tests/run_tests.py}
- **Blockers:** none

### 2026-06-10 (Task 4.1)
- **Focus:** svf-harness Phase 4 Task 4.1 — self-describing schema()
- **Completed:** Schema.{h,cpp} hand-written registry: 67 node_kinds audited from
  toString() prefixes (incl. enum-less aliases FormalINPHISVFGNode/
  ActualOUTPHISVFGNode and newly-found FormalParmPHI/ActualRetPHI), 10 edge_kinds,
  11 methods with param docs + runtime `implemented` flags from methodNames(),
  evidence_record + program blocks; `schema` registered in methodTable.
  Commit c7f0029f.
- **Tests:** 11/11 harness python tests green (new: test_schema_self_describing);
  manual: oneshot schema on demo.ll; invariant cross-checked by script (printed
  toString prefixes == node_kinds, both diffs empty)
- **Files:** svf-llvm/tools/Harness/{Schema.h,Schema.cpp,QueryEngine.h,
  QueryEngine.cpp,CMakeLists.txt,tests/run_tests.py}
- **Blockers:** none

### 2026-06-10 (Task 4.2)
- **Focus:** svf-harness Phase 4 Task 4.2 — callers/callees + review carry-overs
- **Completed:** callers/callees via CallGraphNode in/out edges with per-callsite
  direct/indirect expansion + CallICFGNode evidence; findFunction() with
  Levenshtein did-you-mean hint (≤5 names); indirect.c fixture (explicit stores —
  clang-10 lowers initializer lists to memcpy-from-const which SVF doesn't
  resolve, see plan notes); carry-overs: check_schema_kinds.py text-invariant
  script (67 kinds) wired into run_tests.py, schema test asserts implemented set
  + full method docs. Commit 70338a39.
- **Tests:** 17/17 harness python tests green (new: callers_of_fill,
  callees_of_use_after_free, indirect_callees_resolved, unknown_function_hint,
  schema_kind_invariant); manual: callees(apply) → dbl/neg direct=false;
  unknown 'make_buff' → "did you mean: make_buf, ..."
- **Files:** svf-llvm/tools/Harness/{QueryEngine.h,QueryEngine.cpp,
  tests/run_tests.py,tests/check_schema_kinds.py,tests/fixtures/indirect.c}
- **Blockers:** none

### 2026-06-10 (Task 4.3)
- **Focus:** svf-harness Phase 4 Task 4.3 — cfg/defuse/pts/aliases + resolveVars
- **Completed:** shared resolveVars() anchor helper ({file,line[,name]} /
  {func,ret} / {func,arg}; dedup + id-sort; misses throw accepted-forms +
  nearest-defining-lines hints); cfg (singular findFunction, ambiguity =
  error, node/edge caps 500/1000); defuse (SVFStmt in/out edges as
  {stmt kind, at: ICFG evidence}); pts (Andersen pts → ObjVar evidence);
  aliases (v0 same-function ValVar scope, documented in schema, 50/var cap).
  Query bodies split into Queries.cpp (second TU). Schema returns docs for
  the 4 methods rewritten to real shapes + drift guards. demo.c gained a
  20-arg call covering the Task 2.2 ir-truncation obligation.
  Commit ff2308a1.
- **Tests:** 24/24 harness python tests green (new: cfg_of_use_after_free,
  cfg_ir_truncation, pts_of_b_contains_heap_obj, defuse_of_b,
  aliases_of_malloc_ret, var_resolution_error_hint); manual: pts/defuse/
  aliases of malloc-ret, free arg0 form, line-999/bad-form/arg-range errors,
  cfg ambiguity on dup fixtures
- **Files:** svf-llvm/tools/Harness/{Queries.cpp(new),QueryEngine.h,
  QueryEngine.cpp,Schema.cpp,CMakeLists.txt,tests/run_tests.py,
  tests/fixtures/demo.c}
- **Blockers:** none

### 2026-06-10 (Task 5.1)
- **Focus:** svf-harness Phase 5 Task 5.1 — vfpath/reachable + 4.3 carry-overs
- **Completed:** vfpath (single multi-source BFS over SVFG out-edges, parent
  tree, k=1..10 witness paths — one per distinct sink node, concrete
  SVFGEdge labels + CallICFGNode callsite evidence on Call*/Ret* steps,
  max_visited budget → truncated) and reachable (one shared BFS, ≤20 sinks,
  first_path witnesses) in new VFPath.cpp; sink resolver via
  ICFGNode::getVFGNodes(); fixed latent empty-loc bug (VFGNode sourceLoc is
  never set by SVF — use the ICFG node's). Carry-overs: AnchorDoc.h shared
  anchor prose, defuse defs/uses 200-caps, name-filter-miss +
  {func,name} error messages. Honest BFS limits documented in schema.
  Commit 9f99dd43.
- **Tests:** 30/30 harness python tests green (new: vfpath_malloc_to_use,
  reachable_batch, vfpath_unreachable, reachable_sink_cap,
  anchor_name_filter_miss_hint, func_name_anchor_unsupported); manual:
  malloc→line-11 money shot (RetDirSVFGEdge + callsite@demo.c:8, store 8 →
  IntraInd → load 11), reachable [11 true/6 steps, 22 false]
- **Files:** svf-llvm/tools/Harness/{VFPath.cpp(new),AnchorDoc.h(new),
  Evidence.h,Evidence.cpp,Queries.cpp,QueryEngine.h,QueryEngine.cpp,
  Schema.cpp,CMakeLists.txt,tests/run_tests.py}
- **Blockers:** none

### 2026-06-10 (Task 5.1 hardening — review fixes)
- **Focus:** code-review carry-overs: reachable per-sink errors, vfpath step
  elision, SVF container hardening, BIN existence check
- **Completed:** reachable() per-sink try/catch — bad sink → {reachable:false,
  error} row, good sinks unaffected; buildPath() elideSteps() with
  kPathStepCap=500 + optional max_steps param [10,500]; SVF::Map/SVF::Set for
  Search::parent, targets, nodeToSinks (unordered, ordering from explicit
  sort); BIN file/which check in run_tests.py; Schema.cpp returns docs
  updated. Elision test: chose max_steps param approach (see Next Steps note
  for design rationale). New fixture chain.c: 10-hop store/load chain
  producing a 96-step path. Commit 9ca8b2c3.
- **Tests:** 32/32 harness python tests green (new: reachable_tolerates_bad_sink,
  vfpath_step_elision; test_schema_kind_invariant still skipped as before)
- **Files:** svf-llvm/tools/Harness/{VFPath.cpp,Schema.cpp,tests/run_tests.py,
  tests/fixtures/chain.c(new)}
- **Blockers:** none

### 2026-06-10 (Task 6.1)
- **Focus:** svf-harness Phase 6 Task 6.1 — MCP thin wrapper over daemon socket
- **Completed:** FastMCP stdio server (13 tools: load_program spawns
  `svf-harness serve` with 600s socket wait + stderr surfacing,
  unload_program, 11 query forwards); design decision: STATIC tool
  registration with `schema` tool as single source of truth + generic
  `params` dict per tool (MCP clients list tools at connect, before any
  program loads — dynamic schema()-driven registration impossible; full
  rationale in plan Task 6.1 notes + mcp README); all failures structured
  {"error": ...}, never exceptions; async tools + anyio.to_thread (SDK
  1.27.2 runs sync tools on the event loop). Commit f7e84a5e.
- **Tests:** 3/3 MCP smoke (in-memory transport) + 33/33 full suite (new:
  test_mcp_smoke hook, MCP_PYTHON-gated); manual error-path checks (bad
  binary/bitcode/IR, hint pass-through, daemon-kill recovery, no leaks)
- **Files:** mcp/svf_harness_mcp/{server.py,test_smoke.py,pyproject.toml,
  README.md}(new), svf-llvm/tools/Harness/tests/run_tests.py
- **Blockers:** none

### 2026-06-10 (later)
- **Focus:** svf-harness thin slice implementation (subagent-driven, Tasks 1.1-7.1)
- **Completed:** entire plan — daemon/CLI (11 methods), MCP wrapper, schema invariant
  script, demo. 2 critical bugs caught in review (SVFG use-after-free; daemon
  idle-wedge). Final holistic review: ready to merge.
- **Tests:** 33 python integration tests green; full Test-Suite 2267/2267 serial; demo PASSED
- **Files:** svf-llvm/tools/Harness/* (~2400 LoC C++), mcp/svf_harness_mcp/*, docs/*
- **Blockers:** none. Branch harness-v0 unpushed pending user decision.

### 2026-06-10 (tutorials Phase 1)
- **Focus:** tutorials plan Tasks 1.1 + 1.2 — example scripts 01-04 + run_all,
  tutorials 01-04 written from real runs
- **Completed:** examples/{01-getting-started,02-exploring,03-pointer-dataflow,
  04-value-flow,run_all}.sh (demo conventions: pipefail, tempdir socket, trap,
  bounded waits, assertions, "EXAMPLE NN PASSED"; side fixtures via --oneshot);
  docs/tutorials/{01..04}*.md with all outputs pasted from real runs (anchor
  mental model, HeapObjVar/may-alias/witness-edge-kind/elision interpretations).
  Commits fc62564a (scripts), a02ba571 (tutorials).
- **Tests:** run_all.sh 4/4 PASS + 05 SKIP, rc=0; failure path verified rc=1;
  2 tutorial outputs spot-checked against fresh runs (identical)
- **Files:** svf-llvm/tools/Harness/examples/*(new), docs/tutorials/*(new)
- **Blockers:** none

### 2026-06-10 (shakedown)
- **Focus:** real-program shakedown of svf-harness (user-requested before merge)
- **Completed:** bc.bc (187 fn): 2s load, ~30ms queries. bash.bc (2368 fn, 611k SVFG
  nodes, 2.7GB RSS): 57s load, callers 49ms, vfpath 116ms, reachable batch 340ms,
  aliases 7.1s (known v0 quadratic — FUTURE.md trigger now confirmed). Zero crashes,
  zero wrong results (xmalloc 0-callers anomaly verified correct against llvm-dis:
  this bash build routes all allocation through sh_xmalloc). No-debug-info bitcode
  degrades loc to empty but loc.func + ir keep results usable.
- **Tests:** no code changed; suite remains green (33/33, regression 2267/2267)
- **Blockers:** none. Verdict: merge.

### 2026-06-10 (tutorials Phase 2)
- **Focus:** tutorials plan Tasks 2.1 + 2.2 — real-world example/tutorial 05
  (Test-Suite crux-bc) + MCP tutorial 06 + .mcp.json sample
- **Completed:** examples/05-real-world.sh (bc.bc: timed serve, summary,
  functions alloc|free with all-locs-empty assertion, callers free == 35,
  vfpath malloc→free-arg0 k=2, pts malloc-ret; skip exit-0 when Test-Suite
  absent, SVF_EX05_BC test hook; RUN_BIG=1 bash.bc stretch: xmalloc 0-callers,
  sh_xmalloc 603/200 truncated, vfpath on 611k-node SVFG);
  docs/tutorials/05-real-world-program.md (no-debug-info evidence degradation,
  xmalloc self-correction loop + verified-against-IR sidebar, measured perf
  table); docs/tutorials/06-claude-code-mcp.md (claude mcp add + .mcp.json
  scopes, params-nesting trap demoed live, 3 question patterns, UAF transcript
  with real in-memory-client outputs, troubleshooting);
  examples/mcp-sample.mcp.json (placeholders via "_comment" key). Tutorial 04
  Next-link updated. Commits c6f2e330, 6e59d652.
- **Tests:** ex05 default PASS + RUN_BIG=1 PASS (57s bash load) + skip branch
  exit 0; run_all.sh 5/5 PASS rc=0; py suite 33/33; mcp-sample JSON validated
- **Files:** svf-llvm/tools/Harness/examples/{05-real-world.sh,
  mcp-sample.mcp.json}(new), docs/tutorials/{05-real-world-program.md,
  06-claude-code-mcp.md}(new), docs/tutorials/04-value-flow-witnesses.md
- **Blockers:** none
