# Plan: Harness SABER Checker Surface

## Goal
Expose SVF's SABER source-sink bug checkers through `svf-harness` as
LLM-friendly structured summaries, starting with memory leaks, double-free, and
file-open/close leaks.

## Scope
- Add three daemon/CLI/MCP methods:
  - `saber_leaks`: run SABER memory leak detection.
  - `saber_double_frees`: run SABER double-free detection.
  - `saber_file_leaks`: run SABER file open/close leak detection.
- Return one stable JSON shape:
  `{checker, bugs, total, truncated, sources, sinks}` where each bug includes
  `{type, function, loc, events, description}`.
- Build each SABER checker lazily and cache its summary per daemon.
- Disable SABER stats/slice dumping/validation in query mode so stdout remains
  JSON-only.
- Update schema, CLI help, MCP docs, tutorial counts, and
  `analysis_config.surfaces`.
- Cover behavior with focused fixture/schema tests and small Test-Suite bitcode
  smoke tests for memory leak and double-free.

## Non-Goals
- No full value-flow path reconstruction for SABER bugs in this slice.
- No custom source/sink API configuration.
- No broad SABER sweep across all checker tests.
- No changes to SABER's core analysis algorithm.

## Tasks
- [x] Add failing tests for SABER schema/config/MCP exposure and tool counts.
- [x] Add failing Test-Suite smoke tests for `saber_leaks` and
      `saber_double_frees`.
- [x] Implement SABER checker ownership, option defaults, and JSON conversion.
- [x] Register `saber_leaks`, `saber_double_frees`, and `saber_file_leaks`.
- [x] Update schema/help/MCP/docs and config surface status.
- [x] Build and run focused tests.
- [x] Run full harness Python suite, MCP smoke, examples, and CTest harness.
- [x] Keep Test-Suite generated artifacts clean and update `docs/PROGRESS.md`.

## Verification
- Focused SABER tests fail before implementation and pass after implementation.
- Full `run_tests.py -v` passes.
- Standalone MCP smoke passes with 26 tools.
- `ctest --test-dir Release-build -R harness --output-on-failure` passes with
  clean 2267/2268 harness numbering.

## Results
- Red tests first failed on missing SABER schema methods, `saber` still planned
  in `analysis_config.surfaces`, and unknown `saber_leaks` /
  `saber_double_frees` methods.
- Added `SaberQueries.cpp` to run and cache SABER checker summaries and convert
  `SVFBugReport` bugs/events to stable JSON.
- Added `saber_leaks`, `saber_double_frees`, and `saber_file_leaks` with
  `{checker, bugs, total, truncated, sources, sinks}` output.
- Verification passed:
  - Focused SABER tests: 4/4.
  - Full harness Python suite: 56/56.
  - Standalone MCP smoke: 4/4 using `/home/xiao/program/py311-mcp/bin/python`.
  - Examples: 5/5.
  - CTest harness: 2/2 (`harness_integration` 2267, `harness_examples` 2268)
    after generated-artifact cleanup and CMake reconfigure.
  - Manual `saber_file_leaks` smoke on `demo.c`: returned a valid empty
    checker summary.
- Moved generated Test-Suite `.pre*.bc` artifacts to
  `/tmp/svf-testsuite-generated-after-saber-20260704`; final generated artifact
  count under `Test-Suite/test_cases_bc` is 0.
