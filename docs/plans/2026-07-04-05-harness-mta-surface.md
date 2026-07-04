# Plan: Harness MTA Thread/MHP Surface

## Goal
Expose SVF's multithreaded analysis through `svf-harness` as stable JSON
queries that an LLM can use to inspect thread creation and ask targeted MHP
questions.

## Scope
- Add two daemon/CLI/MCP methods:
  - `mta_summary`: run MTA lazily and return fork/join/TCT/MHP summary data.
  - `mta_mhp`: resolve two source-location anchors and report whether any
    matching ICFG-node pair may happen in parallel.
- Return source-located fork/join sites and TCT thread records where SVF exposes
  them through public APIs.
- Keep query stdout JSON-only even though SVF's MTA implementation writes graph
  dumps and progress text internally.
- Update schema, CLI help, MCP docs, tutorial counts, and
  `analysis_config.surfaces`.
- Cover behavior with focused fixture tests and Test-Suite MTA bitcode smoke.

## Non-Goals
- No full race checker JSON conversion in this slice.
- No custom lockset/race path explanation yet.
- No broad MTA sweep over every Test-Suite MTA case.
- No changes to SVF's core MTA algorithm.

## Tasks
- [x] Add failing tests for MTA schema/config/MCP exposure and tool counts.
- [x] Add failing fixture and Test-Suite smoke tests for MTA summary/MHP query.
- [x] Implement lazy MTA ownership, output/dump isolation, and JSON conversion.
- [x] Register `mta_summary` and `mta_mhp`.
- [x] Update schema/help/MCP/docs and config surface status.
- [x] Build and run focused tests.
- [x] Run full harness Python suite, MCP smoke, examples, and CTest harness.
- [x] Keep generated Test-Suite artifacts clean and update `docs/PROGRESS.md`.

## Verification
- Focused MTA tests fail before implementation and pass after implementation.
- Full `run_tests.py -v` passes.
- Standalone MCP smoke passes with 28 tools.
- `ctest --test-dir Release-build -R harness --output-on-failure` passes.

## Results
- Red tests first failed as intended: schema still had 24 methods,
  `analysis_config.surfaces.mta` was still `planned`, and `mta_summary` /
  `mta_mhp` returned unknown-method errors.
- Added `MTAQueries.cpp` with lazy `MTA` ownership. Query mode disables stats,
  all-pair MHP, and race text output; MTA's unconditional `ptacg.dot` /
  `tcg.dot` writes and progress text are isolated in a temporary directory with
  stdout silenced.
- Added `mta_summary` returning fork/join/TCT/MHP counters plus capped fork,
  join, and thread records.
- Added `mta_mhp` resolving `{file,line[,kind]}` ICFG anchors and returning
  may-happen-in-parallel witnesses.
- Verification passed:
  - Focused MTA tests: 6/6.
  - Full harness Python suite: 60/60.
  - Standalone MCP smoke: 4/4 using `/home/xiao/program/py311-mcp/bin/python`.
  - Examples: 5/5.
  - CTest harness: 2/2 (`harness_integration` 2267, `harness_examples` 2268).
  - Generated Test-Suite `.pre*.bc` artifacts were moved to
    `/tmp/svf-testsuite-generated-after-mta-20260704`; final generated artifact
    count under `Test-Suite/test_cases_bc` is 0.
