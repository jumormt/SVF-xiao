# SVF-xiao — LLM-Friendly Program Analysis Harness

Transforming this SVF fork into the harness envisioned by the DECRA proposal
"LLM-Powered Codebase Reasoning via Static Analysis Tool Synthesis" (~/DECRA_Xiao.pdf):
configuration layer (composable precision), representation layer (queryable code
knowledge base), synthesis layer (NL-driven tool synthesis).

## Current Epic
**E3: svf-harness precision surface expansion** — CLI/MCP now expose active
analysis configuration, SVFG construction can switch between `full` and
`ptr-only`, and CFLAlias plus FlowDDA are available through lazy
`cfl_pts`/`cfl_aliases`, `dda_pts`/`dda_aliases`, SABER checker summary
queries, MTA thread/MHP summaries, and AE trace/state inspection. Full clean
Test-Suite is green after graph/config changes, and harness tests load
representative Test-Suite bitcodes directly.

## Epics
- [x] E1: svf-harness thin slice (v0) — daemon/CLI/MCP, 11 query methods, evidence v0
- [ ] E2: declarative query language L_Q (proposal Task 2.2) — future
- [ ] E3: composable precision configuration (proposal Task 1) — future
- [ ] E4: evidence schema with path conditions / abstract traces (proposal Task 2.3) — future

## Plans Index (active/recent)
| Date | Plan | Epic | Status | Notes |
|------|------|------|--------|-------|
| 2026-07-04 | harness-ae-surface | E3+E4 | done | **Done 2026-07-04.** Added lazy AE-backed `ae_summary`/`ae_state`, schema/help/MCP docs, config surface status, focused fixture + Test-Suite AE smoke tests; focused AE+MCP 6/6, full harness 64/64, MCP smoke 4/4, examples 5/5, ctest harness 2/2. Plan: `docs/plans/2026-07-04-06-harness-ae-surface.md` |
| 2026-07-04 | harness-mta-surface | E3 | done | **Done 2026-07-04.** Added lazy MTA-backed `mta_summary`/`mta_mhp`, schema/help/MCP docs, config surface status, focused fixture + Test-Suite MTA smoke tests; focused MTA 6/6, full harness 60/60, MCP smoke 4/4, examples 5/5, ctest harness 2/2. Plan: `docs/plans/2026-07-04-05-harness-mta-surface.md` |
| 2026-07-04 | harness-saber-surface | E3 | done | **Done 2026-07-04.** Added SABER-backed `saber_leaks`/`saber_double_frees`/`saber_file_leaks`, schema/help/MCP docs, config surface status, focused Test-Suite leak + double-free smoke tests; focused SABER 4/4, full harness 56/56, MCP smoke 4/4, examples 5/5, ctest harness 2/2. Plan: `docs/plans/2026-07-04-04-harness-saber-surface.md` |
| 2026-07-04 | harness-dda-surface | E3 | done | **Done 2026-07-04.** Added lazy FlowDDA-backed `dda_pts`/`dda_aliases`, schema/help/MCP docs, config surface status, focused fixture + Test-Suite smoke tests; focused DDA 5/5, full harness 53/53, MCP smoke 4/4, examples 5/5, ctest harness 2/2. Plan: `docs/plans/2026-07-04-03-harness-dda-surface.md` |
| 2026-07-04 | harness-cfl-surface | E3 | done | **Done 2026-07-04.** Added lazy CFLAlias-backed `cfl_pts`/`cfl_aliases`, schema/help/MCP docs, config surface status, grammar defaulting, alias-validation disable for queries; focused CFL 3/3, full harness 49/49, MCP smoke 4/4, examples 5/5, ctest harness 2/2. Plan: `docs/plans/2026-07-04-02-harness-cfl-surface.md` |
| 2026-07-04 | harness-testsuite-coverage | E1+E3 | done | **Done 2026-07-04.** Added direct Test-Suite bitcode helper/tests for `bc.bc` and `array-3.cpp.bc`; added optional sweep script for all original Test-Suite `.bc`; focused tests 2/2, full harness 45/45, ctest harness 2/2, sweep 787/787. Plan: `docs/plans/2026-07-04-01-harness-testsuite-coverage.md` |
| 2026-07-03 | harness-analysis-config | E3 | done | **Done 2026-07-03.** Added `analysis_config`, CLI/MCP `--analysis-config`, configurable SVFG `full`/`ptr-only`, active config readback; full harness 43/43, MCP smoke 4/4, examples 5/5, ctest harness 2/2, clean full Test-Suite 2268/2268. Plan: `docs/plans/2026-07-03-02-harness-analysis-config.md` |
| 2026-07-03 | harness-graph-queries | E1+ | done | **Done 2026-07-03.** Added `graphs`, `graph_nodes`, `graph_edges`, `node`, `neighbors`; CLI schema/help + MCP static tools aligned at 16 daemon methods / 18 MCP tools; full harness 39/39, MCP smoke 4/4, examples 5/5, ctest harness 2/2. Plan: `docs/plans/2026-07-03-01-harness-graph-queries.md` |
| 2026-06-10 | tutorials | E1+ | done | **All 3 phases done, 2026-06-10.** 6 tutorials (docs/tutorials/ + index), 5 example scripts + run_all (5/5 PASS), 2 ctest entries (harness_integration + harness_examples, both green). Summary: `docs/summaries/2026-06-10-tutorials.md` |
| 2026-06-10 | svf-harness-thin-slice | E1 | done | **All 7 phases done, 2026-06-10.** 11 methods, daemon+CLI+MCP, 33 py tests, full regression 2267/2267, demo green. Summary: `docs/summaries/2026-06-10-svf-harness-thin-slice.md` |

## Next Steps
- **Next B/C slice** — choose between AE detector bug summaries, MTA lock/race
  diagnostics, or deeper DDA/SABER diagnostics.
- **Broader Test-Suite query sweep** — optional next hardening step: run selected
  graph/query methods beyond `summary` across a stratified Test-Suite subset.
- **E2 declarative query language L_Q** (proposal Task 2.2) — still deferred
  unless user redirects.

## Known Issues
- Test-Suite must run SERIALLY (`ctest` without `-j`): parallel runs corrupt shared
  generated `.pre.svf.bc` files → ~106 spurious diff_tests-wr-ander segfaults.
- Before reconfiguring CMake, remove or move ignored generated Test-Suite bitcode
  (`*.pre*.bc`, `*.svf.bc`). If those files exist, `file(GLOB "*.bc*")` can register
  generated artifacts as extra tests; observed polluted list was 3820 tests with
  four spurious `cfl_tests/basic_cpp_tests/array-3.cpp.pre...POCR` failures.
- CFLAlias queries are much heavier than Andersen queries on larger programs.
  Manual `cfl_pts(malloc ret)` on `crux-bc/bc.bc` was interrupted after ~90s;
  keep broad CFL sweeps opt-in with explicit timeout/budget.
- Prebuilt LLVM 21 tarballs need glibc ≥ 2.34; this focal (2.31) machine uses
  conda-forge LLVM via `llvm-21.1.0.obj` symlink → `/home/xiao/program/llvm-21.1.0-conda`.
- Shell env vars `LLVM_DIR`/`Z3_DIR`/`SVF_DIR` point at stale installs; build/test with
  `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR`.

## Key Decisions
| Category | Decision |
|----------|----------|
| Architecture | C++ in-tree tool (`svf-llvm/tools/Harness`), daemon + light CLI over Unix socket JSON-RPC; MCP = thin Python wrapper |
| Precision | Default AndersenWaveDiff + full SVFG; `analysis_config` supports SVFG `full`/`ptr-only`; lazy CFLAlias, FlowDDA, SABER, MTA, and AE query surfaces are available |
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

### 2026-06-10 (tutorials Phase 3 — plan complete)
- **Focus:** tutorials Task 3.1 — harness_examples ctest, docs/tutorials index,
  Phase 2 review carry-overs, full verification, close-out
- **Completed:** CMakeLists harness_examples ctest (BUILD_TESTING-gated, env
  like harness_integration, RUN_BIG unset); docs/tutorials/README.md index
  (6 one-liners, reading order, prereqs, examples/ + MCP links); index linked
  from Harness README ("New here?") and mcp README. Carry-overs: tutorial 06
  relabeled local scope + `-s user` note; Test-Suite clone command added to
  Harness README / ex05 SKIP message / tutorial 05 prereqs. Summary:
  `docs/summaries/2026-06-10-tutorials.md`. **Plan done.**
- **Tests:** `ctest -R "harness_"` 2/2 Passed (integration 3.63s, examples
  3.73s — all 5 scripts PASS incl. 05 on bc.bc); py suite 33/33; link sweep
  37 relative links / 9 md files, 0 broken
- **Files:** svf-llvm/tools/Harness/{CMakeLists.txt,README.md,
  examples/05-real-world.sh}, docs/tutorials/{README.md(new),
  05-real-world-program.md,06-claude-code-mcp.md}, mcp/svf_harness_mcp/README.md
- **Blockers:** none

### 2026-07-03
- **Focus:** LDD plan for svf-harness graph query layer.
- **Completed:** User selected option A (graph browsing) after noting current query
  surface is too small for SVF's many graphs; created
  `docs/plans/2026-07-03-01-harness-graph-queries.md` with phased tasks for
  schema drift tests, core graph browsing methods, MCP/docs updates, and live
  verification. Precision config and DDA/CFL/SABER/MTA/AE explicitly deferred.
- **Tests:** not run for the plan-only step.
- **Files:** docs/PROGRESS.md, docs/plans/2026-07-03-01-harness-graph-queries.md
- **Blockers:** none

### 2026-07-03 (graph queries implementation)
- **Focus:** implement and verify option A: first-class graph browsing through
  CLI and MCP.
- **Completed:** Added five daemon methods (`graphs`, `graph_nodes`,
  `graph_edges`, `node`, `neighbors`) over ICFG, SVFG, SVFIR/PAG, and resolved
  call graph; updated schema docs, CLI help, MCP static wrappers, drift guards,
  examples, tutorials, and user-facing method/tool counts. Deferred precision
  config and DDA/CFL/SABER/MTA/AE surfaces to `docs/FUTURE.md`.
- **Tests:** build ok; full harness suite 39/39; standalone MCP smoke 4/4;
  examples `run_all.sh` 5/5; `ctest -R "harness_"` 2/2; manual daemon run
  exercised `graphs`, `graph_nodes`, and `neighbors` on `demo.c`.
- **Files:** svf-llvm/tools/Harness/{GraphQueries.cpp,QueryEngine.h,
  QueryEngine.cpp,CMakeLists.txt,Schema.cpp,svf-harness.cpp,README.md,
  tests/run_tests.py,examples/02-exploring.sh}; mcp/svf_harness_mcp/{server.py,
  test_smoke.py,README.md}; docs/tutorials/{02-exploring-a-program.md,
  06-claude-code-mcp.md}; docs/FUTURE.md; docs/plans/2026-07-03-01-harness-graph-queries.md
- **Blockers:** none

### 2026-07-03 (analysis config implementation)
- **Focus:** first B/C follow-up: expose active precision/configuration and make
  SVFG construction mode actually configurable.
- **Completed:** Added `analysis_config` daemon method/MCP tool; added
  `--analysis-config JSON` for `serve` and `--oneshot`; MCP `load_program`
  now accepts optional `analysis_config` and returns active config; SVFG builder
  is configured from JSON (`mode: full|ptr-only`, `indirect_calls`, `post_opts`).
  DDA/CFL/SABER/MTA/AE are advertised as planned surfaces, not fake-run.
- **Tests:** red tests observed; build ok; full harness suite 43/43; standalone
  MCP smoke 4/4; examples `run_all.sh` 5/5; `ctest -R "harness_"` 2/2; manual
  daemon run showed ptr-only SVFG active with 87 nodes / 32 edges on `demo.c`.
  Extra verification after user requested more testing: `RUN_BIG=1`
  `examples/05-real-world.sh` passed on `bc.bc` and `bash.bc` (bash load 56s,
  sh_xmalloc witness length 3 over 611k-node SVFG); CLI full-vs-ptr-only graph
  comparison passed (223/170 vs 87/32); invalid CLI/serve configs rejected; MCP
  invalid config rejected and MCP ptr-only load returned 87/32 SVFG.
- **Files:** svf-llvm/tools/Harness/{QueryEngine.h,QueryEngine.cpp,Queries.cpp,
  Schema.cpp,svf-harness.cpp,README.md,tests/run_tests.py,
  examples/02-exploring.sh}; mcp/svf_harness_mcp/{server.py,test_smoke.py,
  README.md}; docs/tutorials/{02-exploring-a-program.md,
  06-claude-code-mcp.md}; docs/FUTURE.md;
  docs/plans/2026-07-03-02-harness-analysis-config.md
- **Blockers:** none

### 2026-07-03 (full Test-Suite shakedown)
- **Focus:** user-requested full Test-Suite run after CLI/MCP graph and config work.
- **Completed:** rebuilt all SVF targets to clear stale non-harness binaries (`cfl`,
  `svf-ex`, etc.); verified prior symbol-lookup failures were stale-binary artifacts;
  ran polluted 3820-test CTest once and identified four generated-artifact CFL POCR
  failures from ignored `.pre` inputs; moved 774 generated bitcode files aside,
  reconfigured to the clean 2268-test suite, reran full serial CTest, and moved the
  newly generated 773 bitcode artifacts aside after the run.
- **Tests:** full rebuild ok; stale-failure representatives passed; polluted run
  `3816/3820` with only generated-artifact `array-3.cpp.pre...POCR` failures; clean
  canonical Test-Suite `2268/2268` passed in 243s. Logs:
  `/tmp/svf-full-ctest-20260703-163555.log`,
  `/tmp/svf-clean-ctest-20260703-164610.log`.
- **Files:** docs/PROGRESS.md; generated Test-Suite artifacts backed up under
  `/tmp/svf-testsuite-generated-20260703-164544` and
  `/tmp/svf-testsuite-generated-after-clean-20260703-165034`.
- **Blockers:** none

### 2026-07-04
- **Focus:** use SVF Test-Suite bitcodes as `svf-harness` test inputs, not only
  upstream expected-output diff tests.
- **Completed:** `run_tests.py` can now query existing `.bc` paths directly;
  added focused Test-Suite-backed tests for `crux-bc/bc.bc` (summary, functions,
  graph inventory, SVFG nodes, ptr-only SVFG) and `basic_cpp_tests/array-3.cpp.bc`
  (summary, function search, callgraph nodes/edges); added optional
  `sweep_testsuite_bitcodes.py` to load all original Test-Suite `.bc` files via
  `svf-harness --oneshot summary`.
- **Tests:** new focused tests 2/2; full harness Python suite 45/45; ctest
  `-R harness` 2/2; optional sweep 787/787 original Test-Suite bitcodes, 0
  failures, elapsed 313.6s.
- **Files:** `svf-llvm/tools/Harness/tests/run_tests.py`,
  `svf-llvm/tools/Harness/tests/sweep_testsuite_bitcodes.py`,
  `docs/plans/2026-07-04-01-harness-testsuite-coverage.md`
- **Blockers:** none.

### 2026-07-04 (CFL surface)
- **Focus:** turn CFL from a planned `analysis_config.surfaces` item into a real
  harness precision surface.
- **Completed:** added lazy CFLAlias construction in `QueryEngine`; added
  `cfl_pts` and `cfl_aliases` daemon/CLI/MCP methods; schema/help/docs now list
  19 daemon methods and 21 MCP tools; config surface `cfl` reports `supported`.
  Harness defaults CFL to source-tree `PAGGrammar.txt` and disables SVF alias-test
  validation for query mode so Test-Suite oracle functions do not abort normal
  harness queries.
- **Tests:** focused CFL 3/3; build target `svf-harness`; full harness Python
  suite 49/49; standalone MCP smoke 4/4; examples 5/5; ctest `-R harness` 2/2.
  Manual C++ Test-Suite CFL `_Znwm` query passed; manual `bc.bc` CFL query was
  interrupted after ~90s and recorded as a performance caveat. After CMake
  reconfigure, generated Test-Suite `.pre*.bc`/`.svf.bc` artifacts were moved
  to `/tmp/svf-testsuite-generated-after-cfl-20260704`; Test-Suite generated
  artifact count is back to 0.
- **Files:** `svf-llvm/tools/Harness/{QueryEngine.h,QueryEngine.cpp,Queries.cpp,
  Schema.cpp,CMakeLists.txt,svf-harness.cpp,README.md}`,
  `svf-llvm/tools/Harness/tests/run_tests.py`,
  `mcp/svf_harness_mcp/{server.py,test_smoke.py,README.md}`,
  `docs/tutorials/{02-exploring-a-program.md,06-claude-code-mcp.md}`,
  `docs/plans/2026-07-04-02-harness-cfl-surface.md`
- **Blockers:** none; next likely surface is DDA or SABER, with CFL broad sweeps
  kept opt-in because of cost.

### 2026-07-04 (DDA surface)
- **Focus:** turn DDA from a planned `analysis_config.surfaces` item into a real
  harness precision surface.
- **Completed:** added lazy FlowDDA construction in `QueryEngine`, keeping a
  persistent `DDAClient` alive for the analysis; disabled SVF stats in DDA query
  mode so CLI stdout remains pure JSON; added `dda_pts` and `dda_aliases`
  daemon/CLI/MCP methods with `analysis: "flowdda"` and the same var-anchor
  contract as Andersen/CFL; schema/help/docs now list 21 daemon methods and 23
  MCP tools; config surface `dda` reports `supported`.
- **Tests:** red DDA tests observed first; build target `svf-harness`; focused
  DDA 5/5; full harness Python suite 53/53; standalone MCP smoke 4/4 using
  `/home/xiao/program/py311-mcp/bin/python`; examples 5/5; ctest `-R harness`
  2/2 with `harness_integration` 2267 and `harness_examples` 2268. Generated
  Test-Suite `.pre*.bc` artifacts from `bc.bc` and `array-3.cpp.bc` were moved
  to `/tmp/svf-testsuite-generated-after-dda-20260704`; final generated artifact
  count is 0.
- **Files:** `svf-llvm/tools/Harness/{QueryEngine.h,QueryEngine.cpp,Queries.cpp,
  Schema.cpp,svf-harness.cpp,README.md}`,
  `svf-llvm/tools/Harness/tests/run_tests.py`,
  `svf-llvm/tools/Harness/examples/02-exploring.sh`,
  `mcp/svf_harness_mcp/{server.py,test_smoke.py,README.md}`,
  `docs/tutorials/{02-exploring-a-program.md,06-claude-code-mcp.md}`,
  `docs/plans/2026-07-04-03-harness-dda-surface.md`
- **Blockers:** none; next likely E3 slices are SABER checker summaries, MTA
  summaries, AE traces, or deeper DDA diagnostics such as budgets/context mode.

### 2026-07-04 (SABER surface)
- **Focus:** turn SABER from a planned `analysis_config.surfaces` item into real
  structured checker-summary queries.
- **Completed:** added `SaberQueries.cpp`; added `saber_leaks`,
  `saber_double_frees`, and `saber_file_leaks` daemon/CLI/MCP methods; each
  method runs the corresponding SABER checker lazily, caches the summary, and
  converts `SVFBugReport` bug/event stacks into JSON `{checker, bugs, total,
  truncated, sources, sinks}`. Query defaults disable SABER stats, slice dumps,
  and test validation for JSON-clean stdout; SABER's existing bug diagnostics
  remain on stderr. Schema/help/docs now list 24 daemon methods and 26 MCP
  tools; config surface `saber` reports `supported`.
- **Tests:** red SABER tests observed first; build target `svf-harness`;
  focused SABER 4/4; full harness Python suite 56/56; standalone MCP smoke 4/4
  using `/home/xiao/program/py311-mcp/bin/python`; examples 5/5; ctest
  `-R harness` 2/2; manual `saber_file_leaks` smoke on `demo.c` returned a
  valid empty checker summary. Generated Test-Suite `.pre*.bc` artifacts from
  `malloc0.c.bc`, `df0.c.bc`, `bc.bc`, and `array-3.cpp.bc` were moved to
  `/tmp/svf-testsuite-generated-after-saber-20260704`; after reconfigure CTest
  numbering is back to `harness_integration` 2267 and `harness_examples` 2268;
  final generated artifact count is 0.
- **Files:** `svf-llvm/tools/Harness/{SaberQueries.cpp,QueryEngine.h,
  QueryEngine.cpp,Schema.cpp,CMakeLists.txt,svf-harness.cpp,README.md}`,
  `svf-llvm/tools/Harness/tests/run_tests.py`,
  `svf-llvm/tools/Harness/examples/02-exploring.sh`,
  `mcp/svf_harness_mcp/{server.py,test_smoke.py,README.md}`,
  `docs/tutorials/{02-exploring-a-program.md,06-claude-code-mcp.md}`,
  `docs/plans/2026-07-04-04-harness-saber-surface.md`
- **Blockers:** none; next likely E3 slices are MTA thread/race summaries, AE
  traces, or richer diagnostics for DDA/SABER (budgets, paths, checker options).

### 2026-07-04 (MTA surface)
- **Focus:** turn MTA from a planned `analysis_config.surfaces` item into real
  thread/MHP query methods.
- **Completed:** added `MTAQueries.cpp`; added `mta_summary` and `mta_mhp`
  daemon/CLI/MCP methods. `mta_summary` runs SVF MTA lazily and returns
  `{analysis, threads, tct_edges, max_context, candidate_functions,
  entry_functions, fork_sites, join_sites, par_for_sites, fork_edges,
  join_edges, mhp_queries, mhp_pairs, forks, joins, thread_records,
  truncated}`. `mta_mhp` resolves two `{file,line[,kind]}` ICFG anchors and
  returns `may_happen_in_parallel` plus capped witness pairs. Query defaults
  disable stats/all-pair/race text output; MTA's unconditional `ptacg.dot` and
  `tcg.dot` writes are isolated in a temporary directory with stdout silenced.
  Schema/help/docs now list 26 daemon methods and 28 MCP tools; config surface
  `mta` reports `supported`.
- **Tests:** red MTA tests observed first; build target `svf-harness`; focused
  MTA 6/6; full harness Python suite 60/60; standalone MCP smoke 4/4 using
  `/home/xiao/program/py311-mcp/bin/python`; examples 5/5; ctest `-R harness`
  2/2 with `harness_integration` 2267 and `harness_examples` 2268. Generated
  Test-Suite `.pre*.bc` artifacts from `malloc0.c.bc`, `df0.c.bc`, `bc.bc`,
  `array-3.cpp.bc`, and `mta/succ_cxt_simple_2.c.bc` were moved to
  `/tmp/svf-testsuite-generated-after-mta-20260704`; final generated artifact
  count is 0.
- **Files:** `svf-llvm/tools/Harness/{MTAQueries.cpp,QueryEngine.h,
  QueryEngine.cpp,Schema.cpp,CMakeLists.txt,svf-harness.cpp,README.md}`,
  `svf-llvm/tools/Harness/tests/{run_tests.py,fixtures/thread_mhp.c}`,
  `svf-llvm/tools/Harness/examples/02-exploring.sh`,
  `mcp/svf_harness_mcp/{server.py,test_smoke.py,README.md}`,
  `docs/tutorials/{02-exploring-a-program.md,06-claude-code-mcp.md}`,
  `docs/plans/2026-07-04-05-harness-mta-surface.md`
- **Blockers:** none; next likely E3 slices are AE traces, MTA lock/race
  diagnostics, or richer DDA/SABER diagnostics.

### 2026-07-04 (future persistent layer)
- **Focus:** record deferred architecture direction from discussion.
- **Completed:** updated `docs/FUTURE.md` to make the persistent layer explicit:
  property-graph snapshots, analysis artifact caching, and optional graph DB or
  embedded-store backing for large/repeated sessions. This remains future work;
  current harness stays an in-memory SVF daemon.
- **Tests:** N/A docs-only.
- **Files:** `docs/FUTURE.md`, `docs/PROGRESS.md`

### 2026-07-04 (AE surface)
- **Focus:** turn AE from a planned `analysis_config.surfaces` item into real
  trace/state inspection queries.
- **Completed:** added `AEQueries.cpp`; added `ae_summary` and `ae_state`
  daemon/CLI/MCP methods. `ae_summary` runs SVF Abstract Execution lazily and
  returns trace coverage, analyzed-function count, AE mode/config strings, and
  aggregate abstract-state entry counts. `ae_state` resolves `{file,line[,kind]}`
  ICFG anchors and returns capped variable/address abstract values with node
  evidence and raw AE state text. AE stdout/stderr is silenced during lazy run
  to preserve JSON-only harness output. Detector bug summaries remain future
  work because existing AE detector reporters are private and need a stable
  structured contract before exposure. Schema/help/docs now list 28 daemon
  methods and 30 MCP tools; config surface `ae` reports `supported`.
- **Tests:** red AE/MCP tests observed first; build target `svf-harness`;
  focused AE+MCP 6/6; full harness Python suite 64/64; standalone MCP smoke
  4/4 using `/home/xiao/program/py311-mcp/bin/python`; examples 5/5; ctest
  `-R 'harness_(integration|examples)'` 2/2 with `harness_integration` 2270
  and `harness_examples` 2271 after current CMake numbering. Generated
  Test-Suite `.pre*.bc` artifacts from AE/C++/crux/SABER/MTA smoke cases were
  removed; final generated artifact count is 0.
- **Files:** `svf-llvm/tools/Harness/{AEQueries.cpp,QueryEngine.h,
  QueryEngine.cpp,Schema.cpp,CMakeLists.txt,svf-harness.cpp,README.md}`,
  `svf-llvm/tools/Harness/tests/{run_tests.py,fixtures/ae_state.c}`,
  `svf-llvm/tools/Harness/examples/02-exploring.sh`,
  `mcp/svf_harness_mcp/{server.py,test_smoke.py,README.md}`,
  `docs/tutorials/{02-exploring-a-program.md,06-claude-code-mcp.md}`,
  `docs/plans/2026-07-04-06-harness-ae-surface.md`
- **Blockers:** none; next likely E3/E4 slices are AE detector bug summaries,
  MTA lock/race diagnostics, or richer DDA/SABER diagnostics.

### 2026-07-04 (skill verification sweep)
- **Focus:** user-requested full test of the SVF harness skills: maintainer
  verification flow plus program-analysis wrapper flow.
- **Completed:** read and followed `svf-harness-maintainer` and
  `svf-program-analysis` skill instructions; rebuilt `svf-harness`; ran the
  full harness, MCP, examples, CTest, and wrapper query paths. During wrapper
  smoke testing, an intermediate check used a nonexistent generated fixture path
  and produced a JSON error object that the ad hoc parser misread as schema;
  rerunning against freshly compiled `/tmp/svf-harness-skill-demo.ll` confirmed
  the current schema shape is still `{methods, node_kinds, edge_kinds, ...}`.
  Then tested the `svf-program-analysis` skill as intended: natural-language
  questions routed to focused harness queries for demo use-after-free
  value-flow, indirect-call resolution, SABER leak/double-free summaries, MTA
  MHP, and AE state inspection.
- **Tests:** `cmake --build Release-build --target svf-harness -j2` passed;
  `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness python3
  svf-llvm/tools/Harness/tests/run_tests.py -v` passed 64/64; standalone MCP
  smoke with `/home/xiao/program/py311-mcp/bin/python` passed 4/4; examples
  `run_all.sh` passed 5/5; CTest `-R 'harness_(integration|examples)'` passed
  2/2; program-analysis wrapper `schema` reported 28 methods and 66 node kinds;
  wrapper `vfpath` from `malloc` return to `demo.c:11` reported 1 witness path
  of length 6. Natural-language analysis smokes: `demo.c` malloc-ret to
  `b[0]` returned a 6-step SVFG witness; `indirect.c` `apply` resolved
  indirect callees `dbl` and `neg`; `malloc0.c.bc` reported 2 SABER leaks;
  `df0.c.bc` reported 1 SABER double-free; `thread_mhp.c` line 6 vs line 13
  returned `may_happen_in_parallel: true`; `ae_state.c` line 10 returned
  `has_state: true` AE matches. Generated Test-Suite `.pre*.bc` artifacts from
  the smoke cases were removed; final generated artifact count is 0.
- **Files:** `docs/PROGRESS.md`
- **Blockers:** none. Existing unrelated worktree entries remain:
  modified `Dockerfile`, untracked `Dockerfile.bk`, and untracked `testcase/`.
