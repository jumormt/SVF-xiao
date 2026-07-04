---
name: svf-harness-maintainer
description: Maintain and extend the SVF-xiao svf-harness CLI/MCP system. Use when Codex needs to add, modify, test, or audit harness query methods, analysis surfaces, CLI help, schema docs, MCP wrapper tools, examples, Test-Suite harness coverage, LDD progress, or prevent drift between svf-harness CLI, daemon schema, and MCP.
---

# SVF Harness Maintainer

## Overview

Use this skill for engineering work on `svf-llvm/tools/Harness`,
`mcp/svf_harness_mcp`, and the LDD docs. Keep CLI, daemon schema, MCP tools,
docs, examples, and tests synchronized.

## Workflow

1. Read `docs/PROGRESS.md` and the relevant `docs/plans/*.md` first.
2. Create or update an LDD plan before substantial code changes.
3. Write or update failing tests before implementation when adding behavior.
4. Implement in the smallest harness surface that matches existing patterns.
5. Update all synchronized surfaces:
   - `QueryEngine.h`
   - `QueryEngine.cpp` method table
   - per-surface `*Queries.cpp`
   - `Schema.cpp`
   - `svf-harness.cpp` help method list
   - `mcp/svf_harness_mcp/server.py`
   - `mcp/svf_harness_mcp/test_smoke.py`
   - README/tutorial/example counts and method lists
   - `analysis_config.surfaces` when a planned surface becomes supported
6. Run focused tests, then full verification.
7. Update LDD with exact test evidence and remaining next steps.

## Verification Commands

Use the repo root as working directory.

```bash
cmake --build Release-build --target svf-harness -j2
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
  python3 svf-llvm/tools/Harness/tests/run_tests.py -v
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
  python3 mcp/svf_harness_mcp/test_smoke.py -v
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
  bash svf-llvm/tools/Harness/examples/run_all.sh
ctest --test-dir Release-build -R 'harness_(integration|examples)' --output-on-failure
find Test-Suite/test_cases_bc -name '*.pre*.bc' -o -name '*.svf.bc' | wc -l
```

Clean generated Test-Suite `.pre*.bc` and `.svf.bc` artifacts after tests if
the count is nonzero.

## Drift Guards

Every implemented daemon method must appear in:

- `QueryEngine::methodTable()`
- `Schema.cpp`
- `svf-harness --help` Methods line
- MCP `_METHODS`
- MCP smoke expected tool set
- README/tutorial/example counts where hard-coded

The full harness test has explicit drift guards for CLI help vs schema and MCP
tool set vs schema. Fix the source of truth instead of weakening these tests.

## Common Extension Patterns

Read `references/surface-patterns.md` when adding a new analysis surface.

Prefer lazy analysis construction for expensive engines. Keep stdout JSON-only
by disabling stats and silencing unavoidable textual output inside the query
adapter. Return capped arrays with `total`/`truncated` whenever results can
grow with program size.
