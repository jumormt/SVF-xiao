# Plan 2026-07-04-06: Harness AE Trace Surface

## Goal
Expose SVF Abstract Execution (AE) through the harness as a queryable precision
surface, starting with trace/state inspection rather than detector reports.

## Scope
- Add `ae_summary`: lazily run AE and return trace coverage plus abstract-state
  map sizes.
- Add `ae_state`: resolve `{file,line[,kind]}` to ICFG nodes and return capped
  abstract-state entries for each matching node.
- Mark `analysis_config.surfaces.ae` as supported.
- Register/document the methods in CLI schema/help and MCP static tools.
- Add focused fixture tests plus one direct Test-Suite AE bitcode smoke.

## Out of Scope
- AE detector bug summaries (`UNSAFE_LOAD`, `UNSAFE_BUFACCESS`) stay future work.
  Existing detector reporters are private and require a cleaner getter/contract
  before exposing structured bugs.
- Persistent storage / graph database layer remains a future plan.

## Tasks
- [x] Write failing tests for schema/config/MCP/static tool coverage and AE
  query behavior.
- [x] Implement lazy AE bootstrap with stdout/stderr isolation suitable for
  JSON-only harness responses.
- [x] Implement `ae_summary` and `ae_state` JSON contracts.
- [x] Update schema, CLI help, MCP docs, tutorials/examples as needed.
- [x] Run focused tests, full harness, MCP smoke, examples, ctest harness, and
  Test-Suite generated-artifact check.

## Acceptance
- Focused AE tests pass.
- Full `svf-llvm/tools/Harness/tests/run_tests.py -v` passes.
- MCP smoke passes and tool set equals daemon schema.
- Example scripts and CTest harness entries still pass.

## Result
Done 2026-07-04. Added `AEQueries.cpp`, lazy AE execution, `ae_summary`,
`ae_state`, schema/help/MCP/docs coverage, focused fixture tests, and one
direct Test-Suite AE bitcode smoke. Final verification: focused AE+MCP 6/6,
full harness 64/64, standalone MCP smoke 4/4, examples 5/5, ctest harness
2/2, generated Test-Suite artifact count 0.
