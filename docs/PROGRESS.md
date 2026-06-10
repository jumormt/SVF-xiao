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
- **svf-harness-thin-slice Phase 3:** start Task 3.1 (HarnessServer + client mode +
  shutdown), Step 1 (failing daemon-lifecycle test `test_daemon_roundtrip` with
  `client(sock, method, params)` JSON-RPC helper over Unix socket). Task 2.2 done
  (commit cebb73e3): `functions(pattern)` + Evidence records + `--params`, 5 python
  tests green. See Task 2.2 implementation notes in the plan for API deltas
  (SVFValue.h moved to SVFIR/, sourceLoc "fl" vs "file" keys, FunObjVar accessors).

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
