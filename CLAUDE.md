# SVF-xiao

Fork of SVF-tools/SVF being transformed into an LLM-friendly program analysis harness
(see `docs/PROGRESS.md` for the roadmap and `docs/designs/` for approved designs).

Codex project instructions live in `AGENTS.md`. This file remains for Claude
Code compatibility and should stay behaviorally aligned with `AGENTS.md`.

## Build & Test (this machine: Ubuntu 20.04 focal)

- Build: `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash ./build.sh`
  (shell env vars point at stale LLVM 16 / old SVF checkout — always unset them;
  `llvm-21.1.0.obj` is a symlink to a conda-forge LLVM 21 that runs on glibc 2.31)
- Test: `source ./setup.sh && cd Release-build && ctest` — **NEVER use `ctest -j`**:
  parallel runs corrupt shared generated `.pre.svf.bc` files and produce ~106 spurious
  diff_tests-wr-ander failures. Clean stale artifacts: `git -C Test-Suite clean -fdx`.
- Upstream remote: `upstream` → SVF-tools/SVF; keep merges conflict-light by keeping
  harness code isolated under `svf-llvm/tools/Harness/` and `mcp/`.

## Session Workflow (MANDATORY)

This project uses the LDD (Living Development Document) methodology for cross-session continuity.
All development state is tracked in `docs/`. Failing to follow this workflow causes context loss.

**The core loop for ALL work: PLAN → IMPLEMENT → UPDATE DOCS.**
No code without a plan. No plan without updating PROGRESS.md. No session ends without documenting.

### On session start — BEFORE any other work
1. Read `docs/PROGRESS.md` in full — understand current epic, plan statuses, next steps, known issues
2. Read the plan file referenced in "Next Steps" — understand phases, tasks, and current position
3. If a design doc is referenced in the plan, read it too
4. State what you're resuming: "Resuming plan <topic> Phase X. Last session completed [Y]. Starting [Z]."

### After planning new work
1. Complex features: use `superpowers:brainstorming` + `superpowers:writing-plans` first, then enhance
   Simple work: write the plan directly
2. Save to `docs/plans/YYYY-MM-DD-NN-<topic>.md` (today's date + daily sequence)
3. Update `docs/PROGRESS.md`:
   - Add the new plan to the Plans Index with status "approved"
   - Update "Next Steps" if this plan is the immediate priority
   - Append to Session Log

### After ANY implementation work (complete OR partial)
1. Update `docs/PROGRESS.md` — this is NOT optional, even if the session is ending:
   - Set plan status: "done" (with summary) or "in-progress" (with what's done, what remains, blockers)
   - If an epic is fully complete: check it off in the Epics list
   - Update "Next Steps" to be specific: "Continue plan <topic> Phase X, Task X.Y"
   - Append to Session Log: date, focus, completed work, test counts, key files changed

### When investigating bugs
1. For non-trivial bugs, create `docs/bugs/NN-<name>.md`
2. For multi-session debugging, create `docs/debug/<topic>.md`
3. Reference bug/debug docs from `docs/PROGRESS.md` Known Issues

### When deferring work
1. Add deferred features to `docs/FUTURE.md` with trigger conditions
2. Note the deferral in the plan's Notes field and in Next Steps
