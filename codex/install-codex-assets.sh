#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
codex_home="${CODEX_HOME:-$HOME/.codex}"
skills_dir="$codex_home/skills"

mkdir -p "$skills_dir"

install_skill() {
  local name="$1"
  local src="$repo_root/codex/skills/$name"
  local dst="$skills_dir/$name"

  if [[ ! -f "$src/SKILL.md" ]]; then
    echo "missing skill source: $src" >&2
    exit 1
  fi

  if command -v rsync >/dev/null 2>&1; then
    mkdir -p "$dst"
    rsync -a --delete "$src/" "$dst/"
  else
    rm -rf "$dst"
    cp -a "$src" "$dst"
  fi
  echo "installed skill: $dst"
}

install_skill svf-program-analysis
install_skill svf-harness-maintainer

cat <<EOF

Codex assets installed.

Next:
  1. Trust this repo in Codex so .codex/config.toml is loaded.
  2. Build Release-build/bin/svf-harness.
  3. Ensure Python can import MCP FastMCP:
       python3 -m pip install mcp
     Or create the venv path the wrapper checks automatically:
       python3 -m venv "$codex_home/venvs/svf-mcp"
       "$codex_home/venvs/svf-mcp/bin/python" -m pip install mcp
  4. Restart Codex, then check: codex mcp list

Optional overrides:
  SVF_MCP_PYTHON=/path/to/python-with-mcp codex
  SVF_HARNESS_BIN=/path/to/svf-harness codex
EOF
