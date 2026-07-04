# Plan: Harness CFL Alias Surface

## Goal
Expose SVF's CFL alias analysis through `svf-harness` as a real precision
surface, so clients can compare Andersen and CFL points-to/alias answers instead
of seeing CFL only as a planned item.

## Scope
- Add two daemon/CLI methods:
  - `cfl_pts`: CFLAlias-backed points-to for the existing var anchor contract.
  - `cfl_aliases`: CFLAlias-backed same-function alias candidates for the
    existing var anchor contract.
- Build CFL lazily on first CFL query so ordinary harness startup stays cheap.
- Document the methods in schema and MCP static tools.
- Update `analysis_config.surfaces` so `cfl` reports supported/available.
- Cover the behavior with focused Python tests and MCP smoke drift checks.

## Non-Goals
- No DDA demand-driven API yet.
- No CFL path/reachability witness surface yet.
- No SABER/MTA/AE checker contracts in this slice.

## Tasks
- [x] Add failing tests for `cfl_pts`, `cfl_aliases`, schema, and MCP method
      exposure.
- [x] Implement lazy CFLAlias ownership in `QueryEngine`.
- [x] Implement `cfl_pts` and `cfl_aliases` query bodies.
- [x] Update schema/help/MCP descriptions and config surface status.
- [x] Build and run focused tests.
- [x] Run full harness Python suite, MCP smoke, CTest harness targets.
- [x] Update `docs/PROGRESS.md`.

## Results
- Focused CFL tests: 3/3 passed (`cfl_pts`, `cfl_aliases`, Test-Suite C++
  `_Znwm` smoke).
- Build: `svf-harness` target passed.
- Full harness Python suite: 49/49 passed.
- Standalone MCP smoke: 4/4 passed, 21 tools listed.
- Example scripts: 5/5 passed.
- CTest harness targets: 2/2 passed (`harness_integration`,
  `harness_examples`).

## Notes
- CFLAlias needs a grammar file. The harness now compiles in the source-tree
  CFL grammar directory and defaults to `PAGGrammar.txt` before lazy CFL build.
- Harness queries disable SVF's alias-test validation before constructing
  CFLAlias. Otherwise Test-Suite bitcodes containing `MUSTALIAS`/`NOALIAS`
  oracle functions can abort a normal query.
- CFLAlias is much heavier than Andersen on larger programs. A manual
  `cfl_pts(malloc ret)` query on `crux-bc/bc.bc` was interrupted after about
  90s; keep large-program CFL checks explicit rather than part of fast tests.

## Verification
- Focused unit tests fail before implementation and pass after implementation.
- Full `run_tests.py -v` passes.
- `ctest --test-dir Release-build -R harness --output-on-failure` passes.
