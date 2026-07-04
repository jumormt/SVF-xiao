# Plan: Harness DDA Surface

## Goal
Expose SVF's demand-driven pointer analysis through `svf-harness`, giving
clients a second real precision surface beyond Andersen and CFLAlias.

## Scope
- Add two daemon/CLI/MCP methods:
  - `dda_pts`: flow-sensitive DDA points-to for the existing var anchor contract.
  - `dda_aliases`: flow-sensitive DDA same-function alias candidates for the
    existing var anchor contract.
- Build FlowDDA lazily on first DDA query and compute points-to demand-by-demand.
- Keep output shape aligned with `pts`, `aliases`, `cfl_pts`, and `cfl_aliases`.
- Update schema, CLI help, MCP docs, and `analysis_config.surfaces`.
- Cover the behavior with focused fixture tests and a small Test-Suite bitcode
  smoke test.

## Non-Goals
- No ContextDDA in this slice.
- No DDA path explanation or budget diagnostics beyond the basic query result.
- No broad Test-Suite DDA sweep.

## Tasks
- [x] Add failing tests for `dda_pts`, `dda_aliases`, schema/config, MCP tool
      exposure, and Test-Suite small-bitcode smoke.
- [x] Implement lazy FlowDDA ownership in `QueryEngine`.
- [x] Implement `dda_pts` and `dda_aliases` query bodies.
- [x] Update schema/help/MCP/docs and config surface status.
- [x] Build and run focused tests.
- [x] Run full harness Python suite, MCP smoke, examples, and CTest harness.
- [x] Keep Test-Suite generated artifacts clean and update `docs/PROGRESS.md`.

## Verification
- Focused tests fail before implementation and pass after implementation.
- Full `run_tests.py -v` passes.
- Standalone MCP smoke passes.
- `ctest --test-dir Release-build -R harness --output-on-failure` passes with
  the clean 2267/2268 harness test numbering.

## Results
- Red tests first failed on missing `dda_pts`/`dda_aliases`, missing schema
  docs, and `analysis_config.surfaces.dda == planned`.
- Added lazy `FlowDDA` ownership in `QueryEngine` with a persistent `DDAClient`
  and query-mode stats disabled for JSON-clean stdout.
- Added `dda_pts` and `dda_aliases` with `analysis: "flowdda"` and the same
  var-anchor/output conventions as Andersen and CFLAlias query methods.
- Verification passed:
  - Focused DDA tests: 5/5.
  - Full harness Python suite: 53/53.
  - Standalone MCP smoke: 4/4 using `/home/xiao/program/py311-mcp/bin/python`.
  - Examples: 5/5.
  - CTest harness: 2/2 (`harness_integration` 2267, `harness_examples` 2268).
- Moved generated Test-Suite `.pre*.bc` artifacts to
  `/tmp/svf-testsuite-generated-after-dda-20260704`; final generated artifact
  count under `Test-Suite/test_cases_bc` is 0.
