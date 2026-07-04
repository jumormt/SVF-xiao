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
| 2026-07-04 | upstream-ci-merge | Infra | done | **Done 2026-07-04.** Merged `upstream/master` to pick up macOS CI Xcode `latest-stable` workflow fixes and current upstream SVF core maintenance changes. Merge had no conflicts; harness build, harness tests 64/64, MCP smoke 4/4, examples 5/5, mdBook checks, and artifact cleanup passed. Plan: `docs/plans/2026-07-04-12-upstream-ci-merge.md` |
| 2026-07-04 | harness-tutorial-redesign | Docs | done | **Done 2026-07-04.** Rebuilt `docs/harness-book/` into a two-layer tutorial: task-driven walkthroughs plus full 28-method query cookbook with stronger coverage checks. Coverage checker, mdBook build, examples 5/5, and artifact cleanup passed. Plan: `docs/plans/2026-07-04-11-harness-tutorial-redesign.md`; design: `docs/designs/2026-07-04-harness-tutorial-redesign.md` |
| 2026-07-04 | codex-mcp-startup | Infra | done | **Done 2026-07-04.** Fixed project-scoped Codex `svf` MCP startup from nested workspaces by launching through `bash -lc`, deriving the Git repo root, and execing `.codex/bin/svf-mcp-server`; wrapper now also discovers `$HOME/program/py311-mcp/bin/python`. MCP smoke 4/4 passed. Plan: `docs/plans/2026-07-04-10-codex-mcp-startup.md` |
| 2026-07-04 | codex-portable-assets | Infra | done | **Done 2026-07-04.** Replaced machine-specific Codex MCP config with repo-local wrapper, vendored installable SVF Codex skills under `codex/skills/`, added `codex/install-codex-assets.sh`, and updated MCP/mdBook/AGENTS guidance. Plan: `docs/plans/2026-07-04-09-codex-portable-assets.md` |
| 2026-07-04 | harness-mdbook-tutorial | Docs | done | **Done 2026-07-04.** Added `docs/harness-book/` mdBook source covering all 28 harness methods, Codex MCP, and Codex skills; added schema coverage checker and linked the book from existing docs. Coverage check, mdBook build, MCP smoke 4/4, examples 5/5 passed. Plan: `docs/plans/2026-07-04-08-harness-mdbook-tutorial.md` |
| 2026-07-04 | ldd-archive | Infra | done | Trimmed `docs/PROGRESS.md` from 598 lines by moving completed plan rows and old session logs to `docs/plans/SESSION-LOG-ARCHIVE.md`. |
| archived | completed plans through 2026-07-04 | mixed | done | Historical completed plans are archived in `docs/plans/SESSION-LOG-ARCHIVE.md`; detailed plan files remain in `docs/plans/`. |

## Next Steps
- **Watch `svf-build` after upstream merge** — confirm the pushed merge clears the macOS `mac-setup` Xcode failure; if it reaches later build/test failures, inspect the new failing job logs separately.
- **Codex portable-assets follow-up** — after push, ask another checkout/user to run `bash codex/install-codex-assets.sh`, trust the repo, build `svf-harness`, and confirm `codex mcp list` plus `$svf-program-analysis` invocation.
- **Next B/C slice** — choose between AE detector bug summaries, MTA lock/race diagnostics, or deeper DDA/SABER diagnostics after tutorial work.
- **E2 declarative query language L_Q** (proposal Task 2.2) — still deferred unless user redirects.

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


## Archive
- Completed plan rows and older session logs are in `docs/plans/SESSION-LOG-ARCHIVE.md`.
- Detailed implementation plans remain in `docs/plans/YYYY-MM-DD-NN-*.md`.
- Post-completion summaries remain in `docs/summaries/`.

## Session Log

### 2026-07-04 (Upstream CI merge)
- **Focus:** address failing GitHub Actions `svf-build #1122` on `focal`.
- **Completed:** inspected the public Actions job and found the macOS job
  failed in `mac-setup` before compilation with `Could not find Xcode version
  that satisfied version spec: '16.0.0'`. Fetched `upstream/master`, confirmed
  upstream already changed the build workflow to `XCODE_VERSION:
  latest-stable` and `xcode-select -p` based symlink discovery, and merged
  `upstream/master` into `focal` with no conflicts. The merge also brings
  recent upstream SVF core maintenance changes.
- **Tests:** `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c 'source
  ./setup.sh > /dev/null && cmake --build Release-build --target svf-harness
  -j2'` passed; harness Python suite passed 64/64; MCP smoke passed 4/4;
  tutorial examples passed 5/5; mdBook coverage checker passed 28/28 methods;
  mdBook build passed; generated Test-Suite artifact count cleaned back to 0.
- **Files:** upstream merge touched `.github/workflows/*`, `build.sh`,
  `setup.sh`, `cmake/Modules/FindZ3.cmake`, and upstream SVF core/LLVM files;
  LDD recorded in `docs/plans/2026-07-04-12-upstream-ci-merge.md` and
  `docs/PROGRESS.md`.
- **Blockers:** local machine lacks `gh`, so CI log inspection used the public
  GitHub Actions web/API surfaces instead.

### 2026-07-04 (Harness tutorial redesign)
- **Focus:** respond to feedback that the current `svf-harness` tutorial is too thin.
- **Completed:** approved and implemented a two-layer documentation design:
  task-driven tutorial chapters plus a full method cookbook. Rewrote the
  mdBook from 738 lines to 2479 lines; added `src/cookbook.md` with all 28
  live schema methods; strengthened `check_coverage.py` to require cookbook
  entries and standard sections; updated Codex MCP docs to match the current
  `bash -lc` config launcher.
- **Tests:** `python3 -m py_compile docs/harness-book/check_coverage.py`
  passed; pre-cookbook coverage check failed as expected with missing
  `cookbook.md`; final coverage checker passed 28/28 methods with cookbook
  entries; `/tmp/svf-mdbook-bin/mdbook build docs/harness-book` using mdBook
  v0.5.3 passed; `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness bash
  svf-llvm/tools/Harness/examples/run_all.sh` passed 5/5; generated
  Test-Suite artifact count cleaned back to 0.
- **Files:** `docs/designs/2026-07-04-harness-tutorial-redesign.md`,
  `docs/plans/2026-07-04-11-harness-tutorial-redesign.md`,
  `docs/harness-book/README.md`, `docs/harness-book/check_coverage.py`,
  `docs/harness-book/src/*.md`, `docs/PROGRESS.md`
- **Blockers:** none.

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

### 2026-07-04 (Codex project migration)
- **Focus:** make the checkout Codex-first while preserving Claude Code
  compatibility.
- **Completed:** added root `AGENTS.md` with build/test, LDD, Codex MCP, and
  svf-harness maintenance guidance; added tracked `.codex/config.toml` for the
  local `svf` MCP server; relaxed `.gitignore` only for that config; kept
  `CLAUDE.md` with a compatibility pointer; updated MCP README, server
  docstring, Harness README, tutorial index, tutorial 05 next link, tutorial 06,
  and the Claude `.mcp.json` sample to lead with Codex and retain Claude Code
  setup.
- **Tests:** `.codex/config.toml` parsed with py311 `tomllib`; Claude sample
  `mcp-sample.mcp.json` parsed with `python3 -m json.tool`; `codex mcp list`
  showed `svf` enabled with `/home/xiao/program/py311-mcp/bin/python` and
  `/home/xiao/project/SVF-xiao/mcp/svf_harness_mcp/server.py`; whitespace check
  on touched files passed. Full `git diff --check` still reports the existing
  unrelated `Dockerfile:40: new blank line at EOF`.
- **Files:** `AGENTS.md`, `.codex/config.toml`, `.gitignore`, `CLAUDE.md`,
  `mcp/svf_harness_mcp/{README.md,server.py}`,
  `svf-llvm/tools/Harness/{README.md,examples/mcp-sample.mcp.json}`,
  `docs/tutorials/{README.md,05-real-world-program.md,06-claude-code-mcp.md}`,
  `docs/plans/2026-07-04-07-codex-project-migration.md`, `docs/PROGRESS.md`
- **Blockers:** none. Existing unrelated worktree entries remain:
  modified `Dockerfile`, untracked `Dockerfile.bk`, and untracked `testcase/`.

### 2026-07-04 (LDD archive)
- **Focus:** archive LDD state before starting mdBook tutorial implementation.
- **Completed:** created `docs/plans/SESSION-LOG-ARCHIVE.md` with the previous completed plan index and full old session log; trimmed `docs/PROGRESS.md` to current state, active next steps, archive pointers, and recent entries.
- **Tests:** docs-only; verified line counts and archive/progress markers.
- **Files:** `docs/PROGRESS.md`, `docs/plans/SESSION-LOG-ARCHIVE.md`
- **Blockers:** none. Existing unrelated worktree entries remain: modified `Dockerfile`, untracked `Dockerfile.bk`, and untracked `testcase/`.

### 2026-07-04 (Harness mdBook tutorial)
- **Focus:** create a comprehensive mdBook tutorial for `svf-harness`.
- **Completed:** added `docs/harness-book/` with mdBook config, README, coverage
  checker, and chapters for setup, concepts, all 28 query methods, Codex MCP,
  Codex skills, troubleshooting, and API cheatsheet. Linked the book from
  `docs/README.md`, `docs/tutorials/README.md`, `svf-llvm/tools/Harness/README.md`,
  and `mcp/svf_harness_mcp/README.md`.
- **Tests:** coverage checker passed with 28/28 live schema methods covered;
  Markdown link check passed; mdBook build passed with temporary mdBook
  v0.5.3 musl binary; MCP smoke passed 4/4; examples `run_all.sh` passed 5/5;
  generated Test-Suite artifact count cleaned back to 0.
- **Files:** `.gitignore`, `docs/harness-book/*`, `docs/README.md`,
  `docs/tutorials/README.md`, `svf-llvm/tools/Harness/README.md`,
  `mcp/svf_harness_mcp/README.md`,
  `docs/plans/2026-07-04-08-harness-mdbook-tutorial.md`, `docs/PROGRESS.md`
- **Blockers:** none. Existing unrelated worktree entries remain:
  modified `Dockerfile`, untracked `Dockerfile.bk`, and untracked `testcase/`.

### 2026-07-04 (Codex portable assets)
- **Focus:** make Codex MCP and skills usable in other users' checkouts instead
  of depending on local `/home/xiao/...` paths.
- **Completed:** changed `.codex/config.toml` to use `bin/svf-mcp-server`;
  added `.codex/bin/svf-mcp-server` to discover repo root, harness binary, and
  Python with `mcp.server.fastmcp`; vendored `svf-program-analysis` and
  `svf-harness-maintainer` under `codex/skills/`; added
  `codex/install-codex-assets.sh`; updated AGENTS, MCP README, and mdBook MCP
  / skills chapters with portable setup instructions.
- **Tests:** pending final verification sweep in this session.
- **Tests:** `.codex/config.toml` parsed with py311 `tomllib`; wrapper and
  installer passed `bash -n`; skill helper passed `py_compile`; temporary
  `CODEX_HOME` install copied both skills; `SVF_MCP_PYTHON=... timeout 3s
  .codex/bin/svf-mcp-server </dev/null` started cleanly; `codex mcp list`
  reported `svf` enabled with `bin/svf-mcp-server`; repo-shipped skill helper
  `schema` query reported 28 methods; mdBook coverage checker passed 28/28;
  mdBook build passed; path scan found no `/home/xiao` or `py311-mcp` in
  portable Codex docs/config; scoped `git diff --check` passed.
- **Files:** `.codex/config.toml`, `.codex/bin/svf-mcp-server`, `.gitignore`,
  `AGENTS.md`, `codex/*`, `mcp/svf_harness_mcp/README.md`,
  `docs/harness-book/src/{mcp-and-codex.md,skills.md}`,
  `docs/plans/2026-07-04-09-codex-portable-assets.md`, `docs/PROGRESS.md`
- **Blockers:** none. Existing unrelated worktree entries remain:
  modified `Dockerfile`, untracked `Dockerfile.bk`, and untracked `testcase/`.

### 2026-07-04 (Codex MCP startup repair)
- **Focus:** fix `svf` MCP startup failure `No such file or directory` after
  the portable-assets config changed the command to `bin/svf-mcp-server`.
- **Completed:** reproduced the bad relative command path from both repo root
  and nested fixture cwd; changed `.codex/config.toml` to launch via
  `bash -lc`, derive the Git repo root, and exec `.codex/bin/svf-mcp-server`;
  added `$HOME/program/py311-mcp/bin/python` to wrapper Python discovery while
  preserving `SVF_MCP_PYTHON` as the first override.
- **Tests:** `.codex/config.toml` parsed with py311 `tomllib`; wrapper
  `bash -n` passed; `codex mcp get svf` and `codex mcp list` report the new
  `bash -lc` launcher; configured startup command from
  `svf-llvm/tools/Harness/tests/fixtures` with closed stdin exited 0; MCP smoke
  passed 4/4.
- **Files:** `.codex/config.toml`, `.codex/bin/svf-mcp-server`,
  `docs/plans/2026-07-04-10-codex-mcp-startup.md`, `docs/PROGRESS.md`
- **Blockers:** none. A running Codex session may need restart/reload for the
  corrected MCP config to be picked up.
