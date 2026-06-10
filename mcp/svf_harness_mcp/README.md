# svf-harness MCP server

A thin [MCP](https://modelcontextprotocol.io) adapter over the `svf-harness`
daemon socket. It does **no analysis itself**: `load_program` spawns
`svf-harness serve <bitcode...>` (which builds SVFIR → Andersen points-to →
SVFG once), and every query tool is a one-line forward of JSON-RPC 2.0 over
the daemon's Unix socket. 13 tools total: `load_program`, `unload_program`,
and the 11 daemon methods (`schema summary functions callers callees cfg
defuse pts aliases vfpath reachable`).

## Setup for Claude Code

Requires python >= 3.10 with the `mcp` SDK (`pip install mcp`) and a built
`svf-harness` binary (see `svf-llvm/tools/Harness/`).

```bash
claude mcp add svf \
    --env SVF_HARNESS_BIN=/path/to/SVF-xiao/Release-build/bin/svf-harness \
    -- /path/to/python-with-mcp /path/to/SVF-xiao/mcp/svf_harness_mcp/server.py
```

On this machine: python is `/home/xiao/program/py311-mcp/bin/python`, repo is
`/home/xiao/project/SVF-xiao`.

## Design: static tools, `schema` as the source of truth

MCP clients list tools **at connect time**, before any program is loaded, so
registering the query tools dynamically from the daemon's `schema()` response
(only available after `load_program`) would leave the client blind. Instead:

- The 11 query tools are registered **statically at import time**, each with a
  short docstring naming the common param keys.
- The daemon's self-describing **`schema` tool stays the single authoritative
  contract**: full param docs, return shapes, all 67 node kinds, 10 edge
  kinds, and the evidence record format. Per-tool docstrings deliberately
  defer to it instead of mirroring it (no drift to maintain).
- Each query tool takes one generic argument, `params: dict`, forwarded
  **verbatim** as JSON-RPC params. New or changed C++-side parameters never
  require touching this wrapper.

Failures never raise: "no program loaded", daemon-side JSON-RPC errors
(`{code, message, hint?}` — the hint is surfaced), and dead-socket conditions
all come back as structured `{"error": ...}` values the LLM can react to.

## Example session

```text
load_program {"bitcode_paths": ["/tmp/demo.ll"]}
  -> {"functions": 9, "icfg_nodes": 93, "pag_nodes": 183, "svfg_nodes": 223,
      "socket_path": "/tmp/svf-mcp-xxxx/svf-1234.sock", "modules": [...]}

schema {}            # the authoritative contract for everything below
  -> {"methods": [...11 methods with params/returns...], "node_kinds": [...],
      "edge_kinds": [...], "evidence_record": {...}, "program": {...}}

vfpath {"params": {"source": {"func": "malloc", "ret": true},
                   "sink": {"file": "demo.c", "line": 11}, "k": 1}}
  -> {"paths": [{"steps": [... malloc ret -> store -> load at line 11,
      each step with kind/id/loc/ir evidence ...]}], "truncated": false}
```

Variable anchors accepted by `defuse`/`pts`/`aliases`/`vfpath`/`reachable`:
`{file, line[, name]}`, `{func, ret: true}`, `{func, arg: N}`.

## Troubleshooting

- **"svf-harness binary not found"** — set `SVF_HARNESS_BIN` to the built
  binary (`Release-build/bin/svf-harness`) or put it on `PATH`.
- **Daemon logs** — stdout/stderr of the spawned daemon go to `daemon.log`
  next to the socket (a `svf-mcp-*` temp dir, path visible in the
  `load_program` result as `socket_path`). `load_program` surfaces the log
  tail automatically if the daemon dies before listening (e.g. invalid IR).
- **"daemon unreachable ... call load_program again"** — the daemon died or
  the socket was removed; just call `load_program` again.
- **Socket cleanup** — `unload_program` (or a replacing `load_program`, or
  server exit) shuts the daemon down and removes the temp dir. Orphans, if
  any, are `svf-mcp-*` dirs under `$TMPDIR` and are safe to delete.
- **Slow `load_program`** — analysis state is built once at serve; big
  programs can take minutes (timeout 600 s). All queries afterwards are fast.

## Tests

```bash
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
    /home/xiao/program/py311-mcp/bin/python mcp/svf_harness_mcp/test_smoke.py -v
```

Also hooked into the C++ suite runner (`svf-llvm/tools/Harness/tests/
run_tests.py`) as `test_mcp_smoke`, gated on the `MCP_PYTHON` interpreter
existing.
