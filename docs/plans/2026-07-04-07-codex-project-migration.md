# Plan: Codex Project Migration

## Goal

Make this SVF-xiao checkout usable as a Codex-first project while preserving
the existing Claude Code workflow.

## Scope

- Add Codex project instructions in `AGENTS.md`.
- Add a project-scoped Codex MCP configuration for the existing
  `svf-harness` MCP server.
- Keep `CLAUDE.md` and Claude Code MCP documentation valid.
- Update SVF Harness docs so Codex is the primary setup path and Claude Code is
  explicitly described as compatible.

## Tasks

- [x] Inspect existing Claude-facing project instructions and MCP docs.
- [x] Confirm current Codex `AGENTS.md` and MCP config behavior from the Codex
  manual and local CLI help.
- [x] Add `AGENTS.md` from the current project workflow.
- [x] Add tracked `.codex/config.toml` for the local SVF MCP server.
- [x] Update Harness/MCP/tutorial docs to lead with Codex and retain Claude
  Code instructions.
- [x] Validate documentation references and Codex MCP config syntax.
- [x] Update `docs/PROGRESS.md` with completed work and verification evidence.

## Verification

- Validate `.codex/config.toml` parses as TOML:
  `/home/xiao/program/py311-mcp/bin/python -c 'import tomllib, pathlib; tomllib.loads(pathlib.Path(".codex/config.toml").read_text()); print("toml ok")'`.
- Validate Claude sample MCP JSON:
  `python3 -m json.tool svf-llvm/tools/Harness/examples/mcp-sample.mcp.json`.
- Confirm Codex sees the project MCP server:
  `codex mcp list` should include `svf` with command
  `/home/xiao/program/py311-mcp/bin/python`.
- Run whitespace checks on touched files:
  `git diff --check -- AGENTS.md .codex/config.toml .gitignore CLAUDE.md docs/plans/2026-07-04-07-codex-project-migration.md docs/PROGRESS.md docs/tutorials/README.md docs/tutorials/05-real-world-program.md docs/tutorials/06-claude-code-mcp.md mcp/svf_harness_mcp/README.md mcp/svf_harness_mcp/server.py svf-llvm/tools/Harness/README.md svf-llvm/tools/Harness/examples/mcp-sample.mcp.json`.
- Note: full `git diff --check` currently fails on pre-existing unrelated
  `Dockerfile:40: new blank line at EOF`.

## Notes

The `svf-harness` binary name should not change. It is the project analysis
tool name, not a Claude-specific name.

## Results

- Added root `AGENTS.md` as the Codex instruction source.
- Added tracked `.codex/config.toml` for the local `svf` MCP server.
- Kept `CLAUDE.md` and `.mcp.json` sample paths as compatibility surfaces.
- Updated Harness/MCP/tutorial docs to lead with Codex and document Claude Code
  compatibility.
- Verified TOML parsing, Claude sample JSON parsing, `codex mcp list`, and
  whitespace checks on touched files.
