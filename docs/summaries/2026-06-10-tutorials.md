# Summary: svf-harness Tutorials

**Plan:** `docs/plans/2026-06-10-02-tutorials.md` (done 2026-06-10)
**Design:** `docs/designs/2026-06-10-tutorials.md`
**Tags:** tutorials, docs, examples, ctest, mcp

## What was built

Six English tutorials (`docs/tutorials/01–06` + `README.md` index) backed by
five executable example scripts (`svf-llvm/tools/Harness/examples/01–05` +
`run_all.sh`) and a second ctest entry (`harness_examples`). Every output
pasted in the tutorials comes from a real run; the scripts assert those
outputs, and ctest keeps them honest in CI. Tutorial 06 (MCP/Claude Code)
has no script — MCP sessions aren't replayable — but all its JSON was
captured via the SDK in-memory client.

## Key decisions (and why)

- **Scripts assert pinned facts, ranges for the rest**: exact numbers only
  where stable (demo.c 9/93/183/223; bc.bc 187 functions, 35 free callsites);
  anything that could drift with SVF asserted as shape/range — tutorials stay
  honest without becoming a regression suite.
- **Side fixtures via `--oneshot`, not a second daemon** (indirect.c in 02,
  chain.c in 04): simpler scripts, and it documents oneshot mode for free.
- **Example 05 skips with exit 0** when Test-Suite is absent (run_all reports
  PASS); the heavy bash.bc stretch sits behind `RUN_BIG=1` so CI stays at
  ~4 s. `SVF_EX05_BC` env hook lets tests exercise the skip branch without
  moving Test-Suite.
- **Tutorial 06's "illustrative transcript, real outputs" framing**: the
  honest middle between a fake transcript and no transcript at all.

## Failed approaches / traps

1. **python < 3.12 rejects backslashes in f-strings** — the scripts' inline
   assertion snippets must avoid escaped quotes inside f-strings (focal ships
   3.8).
2. **MCP `params` nesting trap is silent**: flat `{"pattern": ...}` is not an
   error — pydantic drops the unknown key and the tool runs unfiltered.
   Demoed live in tutorial 06 (total 9 instead of 3) because symptoms beat
   warnings.
3. **`claude mcp add` defaults to local scope, not user** — caught in review;
   docs that say "user scope" for the default command are wrong.

## Reusable patterns

- Script conventions worth copying (from demo/llm_workflow.sh): `set -euo
  pipefail`, tempdir socket, trap cleanup, bounded daemon waits, per-step
  assertions, terminal "EXAMPLE NN PASSED" line, run_all PASS/FAIL table.
- "Docs paste real outputs + companion script asserts them + ctest runs the
  scripts" is the anti-drift loop; apply to any future tutorial work.
- Wherever users can hit a missing dependency (Test-Suite), put the exact
  clone/fix command in the error message itself, not just a README pointer.
