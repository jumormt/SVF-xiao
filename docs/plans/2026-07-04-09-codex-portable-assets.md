# Plan: Portable Codex MCP and Skills Assets

## Goal

Make SVF-xiao usable from another user's Codex environment without relying on
machine-specific `/home/xiao/...` paths. The repo should ship:

- a trusted-project `.codex/config.toml` that starts the `svf` MCP server;
- a repo-local MCP launcher that discovers the checkout, harness binary, and
  Python MCP environment;
- installable Codex skills for program analysis and harness maintenance;
- docs that explain the split between project MCP config and user-installed
  skills.

## Implementation

1. Replace machine-specific `.codex/config.toml` with a portable wrapper entry.
2. Add `.codex/bin/svf-mcp-server` to locate:
   - repo root;
   - `Release-build/bin/svf-harness`, overridable by `SVF_HARNESS_BIN`;
   - Python with `mcp.server.fastmcp`, overridable by `SVF_MCP_PYTHON`.
3. Vendor the verified `svf-program-analysis` and
   `svf-harness-maintainer` skills under `codex/skills/`.
4. Add `codex/install-codex-assets.sh` to copy repo-shipped skills into
   `${CODEX_HOME:-$HOME/.codex}/skills/`.
5. Update README/mdBook/AGENTS guidance to avoid local absolute paths.

## Verification

- Parse `.codex/config.toml`.
- Shell-check scripts with `bash -n`.
- Compile the skill helper Python script.
- Install skills into a temporary `CODEX_HOME`.
- Run the skill helper against `/tmp/svf-mdbook-demo.ll`.
- Start the MCP wrapper with closed stdin.
- Confirm `codex mcp list` reports `svf` as enabled using `bin/svf-mcp-server`.
- Re-run harness book schema coverage after docs updates.

## Status

Done 2026-07-04.
