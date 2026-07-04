# Codex assets

This directory contains repo-shipped Codex assets for SVF-xiao.

- `.codex/config.toml` enables the project-local `svf` MCP server after the
  checkout is trusted.
- `.codex/bin/svf-mcp-server` discovers the repo root, Python MCP SDK, and
  `Release-build/bin/svf-harness` without hard-coded machine paths.
- `codex/skills/` contains installable Codex skills:
  `svf-program-analysis` and `svf-harness-maintainer`.

Install the skills into the current user's Codex home:

```bash
bash codex/install-codex-assets.sh
```

Codex discovers skills from the user's Codex home, not directly from arbitrary
repo directories. The install script copies these repo-shipped skill sources to
`${CODEX_HOME:-$HOME/.codex}/skills/`.

After installation, restart Codex and use:

```bash
python3 -m venv "${CODEX_HOME:-$HOME/.codex}/venvs/svf-mcp"
"${CODEX_HOME:-$HOME/.codex}/venvs/svf-mcp/bin/python" -m pip install mcp
codex mcp list
```

Then invoke the skills:

```text
$svf-program-analysis analyze demo.c: how many functions and ICFG nodes?
$svf-harness-maintainer update a harness query and keep schema/MCP/tests synced
```
