# Codex MCP

Question: how does Codex run the same harness queries without shelling out by
hand?

Codex uses the project MCP server configured in `.codex/config.toml` after the
repository is trusted.

## 1. Inspect The Project Config

Current project config launches through `bash -lc` so nested working
directories still resolve the repository root:

```toml
[mcp_servers.svf]
command = "bash"
args = ["-lc", 'repo_root="$(git rev-parse --show-toplevel)" && cd "$repo_root" && exec .codex/bin/svf-mcp-server']
startup_timeout_sec = 20
tool_timeout_sec = 650
```

The wrapper `.codex/bin/svf-mcp-server` discovers:

- the checkout root;
- `Release-build/bin/svf-harness`, unless `SVF_HARNESS_BIN` overrides it;
- a Python interpreter that can import `mcp.server.fastmcp`, unless
  `SVF_MCP_PYTHON` overrides it.

## 2. Check Server Visibility

From the repository:

```bash
codex mcp list
codex mcp get svf
```

Inside the Codex TUI, `/mcp` shows server status. If the server is missing,
trust the project or restart Codex after changing `.codex/config.toml`.

## 3. Prepare Python If Needed

If the wrapper cannot find a Python environment with MCP installed:

```bash
python3 -m venv "${CODEX_HOME:-$HOME/.codex}/venvs/svf-mcp"
"${CODEX_HOME:-$HOME/.codex}/venvs/svf-mcp/bin/python" -m pip install mcp
```

Or start Codex with explicit overrides:

```bash
SVF_HARNESS_BIN=/path/to/svf-harness \
SVF_MCP_PYTHON=/path/to/python \
codex
```

## 4. Use The MCP Lifecycle

The MCP server does not analyze anything at connection time. The workflow is:

1. `load_program` with LLVM bitcode paths.
2. `schema` once.
3. Query tools such as `functions`, `pts`, `vfpath`, or `graphs`.
4. `unload_program` when done.

`load_program` starts a `svf-harness serve` daemon and forwards query tools to
that daemon over its socket. `unload_program` shuts it down.

## 5. Pass Nested `params`

Every query tool takes a single nested argument:

```json
{"params":{"pattern":"free|malloc"}}
```

The flat form is wrong:

```json
{"pattern":"free|malloc"}
```

The MCP framework may accept the flat form as JSON, but the wrapper drops the
unknown key. The query then runs with empty params, which is a subtle source of
bad results.

## 6. Three Reliable Prompt Patterns

Ask Codex to load and schema first:

```text
Use the svf MCP. Load /tmp/demo.ll, call schema, then list functions matching free|malloc.
```

Ask for evidence, not just conclusions:

```text
Use svf to find a vfpath from malloc return to demo.c:11. Report node kinds,
source lines, edge kinds, and any MAY-analysis caveat.
```

Ask for cleanup:

```text
Unload the current svf program when done.
```

## Common Pitfalls

- Starting Codex before trusting the project.
- Forgetting to rebuild `Release-build/bin/svf-harness`.
- Passing flat MCP arguments instead of nested `params`.
- Expecting the MCP server to do analysis before `load_program`.
