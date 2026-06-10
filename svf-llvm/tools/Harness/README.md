# svf-harness — LLM-friendly SVF query daemon/CLI

`svf-harness` exposes SVF's program analyses (call graph, ICFG, points-to,
sparse value-flow) as a query service an LLM agent can drive: a daemon builds
`SVFIR → Andersen → SVFG` once for a set of LLVM bitcode modules and answers
newline-delimited JSON-RPC 2.0 requests over a Unix socket; every graph node
in a response carries a uniform `{kind, id, loc, ir}` evidence record, and a
self-describing `schema` method documents every node kind, edge kind, and
query method so the client never has to guess the contract. Design rationale:
[`docs/designs/2026-06-10-svf-harness-thin-slice.md`](../../../docs/designs/2026-06-10-svf-harness-thin-slice.md).

## Quick start

Build (from the repo root; this machine needs the env wrapper, see the
top-level `CLAUDE.md`):

```bash
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && cmake --build Release-build -j8 --target svf-harness'
```

Compile something to analyze and start the daemon:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names -o demo.ll \
      svf-llvm/tools/Harness/tests/fixtures/demo.c
Release-build/bin/svf-harness serve demo.ll --socket /tmp/h.sock &
```

Query it (the CLI is a thin client; it prints the bare JSON result):

```bash
$ Release-build/bin/svf-harness summary --socket /tmp/h.sock
{"functions":9,"icfg_nodes":93,"pag_nodes":183,"svfg_nodes":223}

$ Release-build/bin/svf-harness functions --params '{"pattern": ".*free.*"}' --socket /tmp/h.sock
{"functions":[{"is_decl":true,"loc":{"file":"","line":0},"name":"free","num_args":1},
              {"is_decl":false,"loc":{"file":".../demo.c","line":6},"name":"use_after_free","num_args":0}],
 "total":2,"truncated":false}

$ Release-build/bin/svf-harness vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"file": "demo.c", "line": 11}, "k": 1}' \
    --socket /tmp/h.sock
{"paths":[{"length":6,"steps":[
   {"edge":null,              "node":{"kind":"AddrVFGNode",     "loc":{"file":".../demo.c","func":"make_buf","line":4}, ...}},
   ...
   {"edge":"RetDirSVFGEdge",  "node":{"kind":"ActualRetVFGNode","loc":{...,"line":8}, ...},
    "callsite":{"kind":"CallICFGNode","loc":{...,"func":"use_after_free","line":8}, ...}},
   {"edge":"IntraDirSVFGEdge","node":{"kind":"StoreVFGNode",    "loc":{...,"line":8}, ...}},
   {"edge":"IntraIndSVFGEdge","node":{"kind":"LoadVFGNode",     "loc":{...,"line":11}, ...}}]}],
 "sinks":4,"sources":1,"truncated":false,"visited":5}
```

Shut down:

```bash
Release-build/bin/svf-harness shutdown --socket /tmp/h.sock
```

A scripted version of this whole workflow lives in
[`demo/llm_workflow.sh`](demo/llm_workflow.sh) (see Testing below).

## Protocol

**Transport.** Newline-delimited JSON-RPC 2.0 over a Unix stream socket: one
request line in, one response line out, per connection. Requests are capped at
1 MiB. The daemon is **serial** — one request is served at a time.

```
→ {"jsonrpc": "2.0", "id": 1, "method": "functions", "params": {"pattern": "main"}}
← {"jsonrpc": "2.0", "id": 1, "result": {"functions": [...], "total": 1, "truncated": false}}
```

**Errors** follow JSON-RPC: `{"jsonrpc":"2.0","id":...,"error":{"code","message"[,"data"]}}`.

| Code   | Meaning                                                            |
|--------|--------------------------------------------------------------------|
| -32700 | Parse error — request line is not valid JSON (`id` is `null`)      |
| -32600 | Invalid request — missing string `method`, or request over 1 MiB   |
| -32601 | Method not found — `data.hint` lists the known methods             |
| -32000 | Server/query error — bad params, unknown function/var anchor, caps exceeded, receive timeout |

(-32602 is not used; parameter problems are reported as -32000 with a
descriptive message.) **Hint convention:** wherever the harness can guess what
you meant, the error carries actionable guidance — unknown methods get a
`data.hint` with the method list, unknown function names get "did you mean"
suggestions, failed var anchors get the accepted anchor forms plus the nearest
defining lines, in the `message` itself.

**Socket resolution** (same logic on both sides):
1. `--socket PATH` argument
2. `SVF_HARNESS_SOCKET` environment variable
3. default: `/tmp/svf-harness-<id>.sock` where `<id>` = first 12 hex chars of
   the FNV-1a 64 hash of the absolute module paths (server side); the client
   falls back to the single `/tmp/svf-harness-*.sock` if exactly one exists,
   else errors with a hint.

**Oneshot mode** — no daemon, no socket; builds the analysis, answers one
query on stdout, exits (CI-friendly):

```bash
svf-harness --oneshot <method> [--params JSON] <bitcode...>
```

**CLI output contract.** Client subcommands and oneshot print the bare
`result` JSON on stdout and exit 0; on failure they print the JSON-RPC error
object (`{code, message[, data]}` — oneshot wraps it as `{"error": {...}}`)
and exit 1. Stdout is always pure JSON; diagnostics go to stderr.

## Method reference

Eleven methods: `schema summary functions callers callees cfg defuse pts
aliases vfpath reachable`. **The authoritative reference is the tool itself**
— `svf-harness schema` returns full documentation for every method (params,
return shapes, semantics, caps), all 67 node kinds, 10 edge kinds, the
evidence-record contract, and the loaded program; this README deliberately
does not duplicate it. Trimmed excerpts of what `schema` returns:

A node kind entry:

```json
{
  "name": "LoadVFGNode",
  "graph": "vfg",
  "attributes": ["kind", "id", "loc", "ir"],
  "description": "A read through a pointer: p = *q. The typical SINK of use-after-free / uninitialized-read queries; its loc is the dereference site."
}
```

A method entry (trimmed):

```json
{
  "name": "functions",
  "implemented": true,
  "description": "List functions by name pattern. Use it to resolve the exact symbol names (and whether they are definitions or external declarations) before calling callers/callees/cfg.",
  "params": {
    "pattern": {
      "type": "string", "required": false,
      "description": "ECMAScript regex matched anywhere in the function name (search semantics). Omit or empty = all functions."
    }
  },
  "returns": "{functions: [{name, loc, is_decl, num_args}], total, truncated} (capped at 200, sorted by name)"
}
```

The evidence record attached to every returned graph node:

```json
{
  "description": "Uniform record attached to every graph node a method returns. Always check `loc` to map a result to source, and `kind` against node_kinds to interpret what the node means.",
  "fields": {
    "kind": "Node class name; one of node_kinds[].name.",
    "id": "Stable numeric node id within this daemon's loaded program. Valid only for this program instance — do not persist across reloads.",
    "loc": {
      "file": "Source file path; \"\" when the node has no source counterpart (declarations, globals, synthetic and memory-SSA nodes).",
      "line": "1-based source line; 0 when unknown (same cases as empty file).",
      "func": "Enclosing function name; \"\" for global-scope nodes."
    },
    "ir": "The node's SVF textual dump (class name + statement text). Truncated to ~200 bytes with a trailing … (horizontal ellipsis) when longer; truncation never splits a UTF-8 codepoint."
  }
}
```

## Known quirks

- **`.pre.bc` filesystem side effect.** SVF's `preProcessBCs` writes
  preprocessed bitcode (`<input>.pre.bc` etc.) **next to the input files**.
  Analyzing files in a read-only or shared directory will fail or litter it —
  copy inputs to a scratch directory first (the tests and the demo do this).
- **Serial daemon.** One request at a time; a long `vfpath` blocks the next
  caller. A client that connects but does not send a full request line within
  the receive timeout gets -32000 "request timed out".
- **May-analysis semantics.** Results over-approximate: a `vfpath` witness or
  an alias pair is a MAY fact from Andersen's analysis + memory SSA —
  evidence to inspect, not proof of a bug. `vfpath`'s BFS returns at most one
  shortest path per distinct sink node (see the schema description).
- **`aliases` v0 scope.** Alias candidates are top-level `ValVar`s of the
  queried var's *own function* only; cross-function aliases are out of scope
  in v0 (use `pts` and compare objects instead).

## Testing

All commands from the repo root; the harness tests need `clang` on PATH, so
source `setup.sh` first (ctest inherits the caller's environment — the
`harness_integration` test only injects `SVF_HARNESS_BIN`, not a compiler):

```bash
# the python suite directly (33 tests)
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
   python3 svf-llvm/tools/Harness/tests/run_tests.py -v'

# the same suite via ctest (registered as harness_integration)
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && cd Release-build && ctest -R harness_integration'

# the end-to-end demo
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
   bash svf-llvm/tools/Harness/demo/llm_workflow.sh'
```

The MCP smoke test runs as part of the suite when a Python ≥3.10 with the
`mcp` SDK is available (`MCP_PYTHON` env var); otherwise it skips cleanly.

**MCP wrapper** (use the harness as MCP tools from Claude Code etc.):
see [`mcp/svf_harness_mcp/README.md`](../../../mcp/svf_harness_mcp/README.md).
