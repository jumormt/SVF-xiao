# SVF-xiao

Fork of SVF-tools/SVF being transformed into an LLM-friendly program analysis
harness. See `docs/PROGRESS.md` for the roadmap and `docs/designs/` for
approved designs.

This file is the Codex project instruction source. Claude Code compatibility is
kept in `CLAUDE.md`.

## Build & Test

This machine is Ubuntu 20.04 focal.

- Build: `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash ./build.sh`
  - Shell env vars point at stale LLVM 16 / old SVF checkout. Always unset
    `LLVM_DIR`, `Z3_DIR`, and `SVF_DIR`.
  - `llvm-21.1.0.obj` is a symlink to a conda-forge LLVM 21 that runs on
    glibc 2.31.
- Incremental harness build:
  `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c 'source ./setup.sh > /dev/null && cmake --build Release-build --target svf-harness -j2'`
- Harness tests:
  `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness python3 svf-llvm/tools/Harness/tests/run_tests.py -v`
- MCP smoke:
  `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness python3 mcp/svf_harness_mcp/test_smoke.py -v`
- Examples:
  `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness bash svf-llvm/tools/Harness/examples/run_all.sh`
- CTest harness entries:
  `ctest --test-dir Release-build -R 'harness_(integration|examples)' --output-on-failure`
- Full Test-Suite: `source ./setup.sh && cd Release-build && ctest`
  - Never use `ctest -j`; parallel runs corrupt shared generated
    `.pre.svf.bc` files and produce spurious failures.
  - Clean stale artifacts with `git -C Test-Suite clean -fdx` when needed.

Upstream remote: `upstream` points to `SVF-tools/SVF`. Keep merges
conflict-light by keeping harness code isolated under
`svf-llvm/tools/Harness/` and `mcp/`.

## Codex MCP

Project-scoped Codex MCP configuration lives in `.codex/config.toml` and points
at `mcp/svf_harness_mcp/server.py`. It is loaded only when Codex trusts this
project. In Codex, use `/mcp` or `codex mcp list` to inspect the active server.

The server starts no analysis at connection time. Call `load_program` with LLVM
bitcode paths first, then call `schema` once and treat it as the authoritative
contract for all query methods.

For C/C++ source questions, compile to LLVM IR with debug info and value names:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names -o /tmp/input.ll input.c
```

## Session Workflow

This project uses the LDD (Living Development Document) methodology for
cross-session continuity. All development state is tracked in `docs/`.

Core loop: PLAN -> IMPLEMENT -> UPDATE DOCS. No code without a plan. No plan
without updating `docs/PROGRESS.md`. No session ends without documenting.

### On Session Start

1. Read `docs/PROGRESS.md` in full.
2. Read the plan file referenced in "Next Steps" or the active/recent Plans
   Index entry relevant to the task.
3. If a design doc is referenced in the plan, read it too.
4. State what you are resuming: "Resuming plan <topic> Phase X. Last session
   completed [Y]. Starting [Z]."

### After Planning New Work

1. Complex features: use brainstorming and a written implementation plan.
   Simple work: write a direct LDD plan.
2. Save plans to `docs/plans/YYYY-MM-DD-NN-<topic>.md`.
3. Update `docs/PROGRESS.md`:
   - Add the new plan to the Plans Index with status `approved` or
     `in-progress`.
   - Update "Next Steps" if this plan is the immediate priority.
   - Append to Session Log.

### After Implementation Work

Update `docs/PROGRESS.md`, even for partial work:

- Set plan status to `done` with summary, or `in-progress` with completed work,
  remaining work, and blockers.
- If an epic is fully complete, update the Epics list.
- Update "Next Steps" to a specific continuation.
- Append to Session Log with date, focus, completed work, test counts, key
  files changed, and blockers.

### Bug Investigations

- For non-trivial bugs, create `docs/bugs/NN-<name>.md`.
- For multi-session debugging, create `docs/debug/<topic>.md`.
- Reference bug/debug docs from `docs/PROGRESS.md` Known Issues.

### Deferred Work

- Add deferred features to `docs/FUTURE.md` with trigger conditions.
- Note the deferral in the plan's Notes field and in Next Steps.

## SVF Harness Maintenance

When changing `svf-llvm/tools/Harness` or `mcp/svf_harness_mcp`, keep these
surfaces synchronized:

- `QueryEngine.h`
- `QueryEngine.cpp` method table
- per-surface `*Queries.cpp`
- `Schema.cpp`
- `svf-harness.cpp` help method list
- `mcp/svf_harness_mcp/server.py`
- `mcp/svf_harness_mcp/test_smoke.py`
- README/tutorial/example method counts and method lists
- `analysis_config.surfaces` when a planned surface becomes supported

The full harness test has drift guards for CLI help vs schema and MCP tool set
vs schema. Fix the source of truth instead of weakening those tests.

## Worktree Safety

- Do not revert user changes unless explicitly requested.
- Current unrelated local changes may exist; inspect with `git status --short`
  and avoid touching unrelated files.
- Never run destructive Git commands such as `git reset --hard` or
  `git checkout --` unless the user explicitly requests them.
