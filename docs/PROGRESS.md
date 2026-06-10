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
- [ ] E1: svf-harness thin slice (v0) — daemon/CLI/MCP, 11 query methods, evidence v0
- [ ] E2: declarative query language L_Q (proposal Task 2.2) — future
- [ ] E3: composable precision configuration (proposal Task 1) — future
- [ ] E4: evidence schema with path conditions / abstract traces (proposal Task 2.3) — future

## Plans Index (active/recent)
| Date | Plan | Epic | Status | Notes |
|------|------|------|--------|-------|
| 2026-06-10 | svf-harness-thin-slice | E1 | in-progress | Design: `docs/designs/2026-06-10-svf-harness-thin-slice.md` (user-approved). Plan: `docs/plans/2026-06-10-01-svf-harness-thin-slice.md`. |

## Next Steps
- **svf-harness-thin-slice Phase 4:** start Task 4.1 (`schema()`), Step 1 (failing
  test `test_schema_self_describing`: node_kinds/edge_kinds/methods with non-empty
  descriptions, 11 methods). Mind the review-decision in the plan: node_kinds must
  derive from `toString()` prefixes (incl. `FormalINPHISVFGNode`/`ActualOUTPHISVFGNode`
  aliases), not GNodeK enums alone; `schema().methods` should reuse
  `QueryEngine::methodNames()`/method table added in Task 3.1. Task 3.1 done (commit
  227e72ee): daemon + client mode, 9 python tests green — see Task 3.1 implementation
  notes in the plan (socket resolution, error contracts, method registry).

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
