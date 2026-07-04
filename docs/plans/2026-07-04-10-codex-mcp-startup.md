# Plan: Codex MCP Startup Repair

**Epic:** Infra
**Design:** N/A

## Summary

Repair the project-scoped Codex `svf` MCP startup path so it works when Codex is
started from a nested repository directory. The current config advertises a
relative `bin/svf-mcp-server` command, but Codex launches it without a configured
`cwd`, producing `No such file or directory` before the wrapper can run.

**Decisions locked in:**
- Keep the repository-local `.codex/bin/svf-mcp-server` wrapper as the single
  MCP entrypoint.
- Avoid absolute checkout paths in `.codex/config.toml`; derive the repo root
  from Git at startup.

---

## Phase 1: Repair Startup

Make MCP launch independent of the shell's current subdirectory and ensure this
machine's existing MCP Python can be discovered.

### [x] Task 1.1: Fix Codex command path
- [x] In `.codex/config.toml`, launch through `bash -lc` and `cd` to
  `git rev-parse --show-toplevel` before executing `.codex/bin/svf-mcp-server`.
- [x] Verify `codex mcp get svf` reports the expected command and args.

### [x] Task 1.2: Improve Python discovery
- [x] In `.codex/bin/svf-mcp-server`, include
  `$HOME/program/py311-mcp/bin/python` in the candidate list so this machine's
  verified MCP interpreter is found without a manual environment variable.
- [x] Keep `SVF_MCP_PYTHON` as the first override.

### [x] Task 1.3: Tests
- [x] Reproduce the old bad command path failure.
- [x] Verify the new configured command starts the MCP server from a nested
  repository directory with closed stdin.
- [x] Parse `.codex/config.toml`, run `bash -n` on the wrapper, and re-run MCP
  smoke.

## Verification

- [x] `codex mcp get svf`
- [x] configured startup command from `svf-llvm/tools/Harness/tests/fixtures`
- [x] `/home/xiao/program/py311-mcp/bin/python mcp/svf_harness_mcp/test_smoke.py -v`
- [x] `docs/PROGRESS.md` updated with results
