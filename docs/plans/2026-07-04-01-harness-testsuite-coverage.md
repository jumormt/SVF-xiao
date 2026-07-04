# Plan: Harness Test-Suite Bitcode Coverage

## Goal
Exercise `svf-harness` directly on representative SVF Test-Suite bitcode files,
so CLI/MCP graph and query coverage is tested against real prebuilt programs,
not only tiny generated fixtures.

## Scope
- Add unittest coverage that loads Test-Suite `.bc` files directly.
- Keep tests skipped when the optional Test-Suite bitcode directory is absent.
- Cover real-program summary, function search, graph inventory, graph node/edge
  queries, and `svfg.mode = ptr-only`.
- Reuse existing `run_tests.py` and CTest `harness_integration` path.

## Tasks
- [x] Add failing tests that call a direct-bitcode helper.
- [x] Implement the direct-bitcode helper.
- [x] Run selected new tests.
- [x] Run the full harness Python suite.
- [x] Run CTest harness targets.
- [x] Add and run an optional Test-Suite bitcode sweep script.
- [x] Update `docs/PROGRESS.md`.

## Results
- New focused tests: 2/2 passed.
- Full harness Python suite: 45/45 passed.
- CTest harness targets: 2/2 passed (`harness_integration`,
  `harness_examples`).
- Optional sweep: 787/787 original Test-Suite `.bc` files passed
  `svf-harness --oneshot summary` with 0 failures.

## Test Inputs
- `Test-Suite/test_cases_bc/crux-bc/bc.bc`: larger C real-world sample.
- `Test-Suite/test_cases_bc/basic_cpp_tests/array-3.cpp.bc`: C++ sample for
  callgraph smoke coverage.

## Notes
- These tests are harness tests: they use Test-Suite bitcode as input programs.
  They do not run the upstream Test-Suite expected-output diff harness.
- Assertions should avoid debug-location assumptions because many Test-Suite
  bitcodes carry little or no debug info.
