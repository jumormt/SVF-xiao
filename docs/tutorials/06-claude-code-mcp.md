# Tutorial 06 — Claude Code via MCP

## Goal

Wire the harness into [Claude Code](https://docs.anthropic.com/en/docs/claude-code)
through the [MCP](https://modelcontextprotocol.io) wrapper, so the loops you
ran by hand in tutorials 01–05 — load, schema, question, evidence-grounded
answer — happen inside an LLM conversation. You will register the server
(two ways), learn the one trap in the tool-call format, see three question
patterns that work well, and read a sample session.

There is no companion script for this tutorial: an MCP session is an
interactive LLM conversation and cannot be replayed deterministically.
What *is* reproducible: every tool call shown below was executed for real
against the wrapper (via the MCP SDK's in-memory client — the same
transport `mcp/svf_harness_mcp/test_smoke.py` uses), and the JSON outputs
are pasted unedited. Only the English prose around them is illustrative.

## Prerequisites

- A built `svf-harness` binary (`Release-build/bin/svf-harness`) — Tutorial 01.
- A python ≥ 3.10 with the MCP SDK: `pip install mcp`. On this machine
  that interpreter is `/home/xiao/program/py311-mcp/bin/python` — substitute
  your own everywhere it appears.
- Claude Code installed (`claude` on PATH).

## Steps

### 1. Register the server — user scope

From the repo root (so `$PWD` expands to absolute paths):

```bash
claude mcp add svf \
    --env SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
    -- /home/xiao/program/py311-mcp/bin/python $PWD/mcp/svf_harness_mcp/server.py
```

Anatomy: everything after `--` is the server command (the python
interpreter running `server.py` over stdio); `--env` hands the wrapper the
path to the C++ binary it will spawn on `load_program`. Nothing heavy
happens at registration or connect — the wrapper is a thin adapter and
starts no daemon until you load a program.

Verify with `claude mcp list`, or `/mcp` inside a session — you should see
`svf` connected with 13 tools.

### 2. Alternative: project-scope `.mcp.json`

To share the config with a project (checked in, applies to anyone running
`claude` in that directory), copy
[`examples/mcp-sample.mcp.json`](../../svf-llvm/tools/Harness/examples/mcp-sample.mcp.json)
to the project root as `.mcp.json`:

```json
{
  "mcpServers": {
    "svf": {
      "command": "${PYTHON_WITH_MCP}",
      "args": ["${SVF_REPO}/mcp/svf_harness_mcp/server.py"],
      "env": {
        "SVF_HARNESS_BIN": "${SVF_REPO}/Release-build/bin/svf-harness"
      }
    }
  }
}
```

Replace the two placeholders by hand before use — `${SVF_REPO}` with your
absolute checkout path and `${PYTHON_WITH_MCP}` with your MCP-capable
interpreter; they are placeholders for *you*, not variables anything
expands. (The sample file carries the same instructions in a `"_comment"`
key — JSON has no comment syntax; delete that key if your tooling objects.)
Claude Code will prompt once to approve the project server.

### 3. The tool surface — and the one trap

The server exposes **13 tools**: `load_program` / `unload_program`
(lifecycle), plus the 11 daemon methods you already know — `schema`,
`summary`, `functions`, `callers`, `callees`, `cfg`, `defuse`, `pts`,
`aliases`, `vfpath`, `reachable`.

Two design facts (rationale in
[`mcp/svf_harness_mcp/README.md`](../../mcp/svf_harness_mcp/README.md))
shape how you use them:

**Fact 1 — every query tool takes a single `params` dict, nested.** The
arguments must be wrapped under a `"params"` key:

```json
{"params": {"pattern": "free|alloc"}}     ← correct
{"pattern": "free|alloc"}                 ← WRONG: silently drops the pattern
```

This is the trap (documented in the tool docstrings themselves): the
flat form is not an error — pydantic just discards the unknown key and the
tool runs with empty params. Demonstrated for real on demo.ll:

```json
// functions {"params": {"pattern": "free|alloc"}}  -> the 3 matching functions
{"functions": [{"name": "free", ...}, {"name": "malloc", ...},
               {"name": "use_after_free", ...}], "total": 3, "truncated": false}

// functions {"pattern": "free|alloc"}  -> pattern dropped, ALL 9 functions
{"functions": [{"name": "fill", ...}, {"name": "free", ...},
               {"name": "llvm.memset.p0.i64", ...}, "...6 more..."],
 "total": 9, "truncated": false}
```

If a result looks suspiciously unfiltered, check the nesting first.

**Fact 2 — `schema` is the contract; docstrings are only signposts.** The
11 query tools are registered statically (MCP clients list tools before
any program is loaded), so their docstrings are deliberately short. The
schema-first workflow an LLM should follow — and that you should put in
your `CLAUDE.md` or prompt for serious sessions: after `load_program`,
call `schema` once, and treat its `methods` / `node_kinds` / `edge_kinds`
/ `evidence_record` blocks as the authoritative reference for every later
call. This is exactly the self-describing contract from Tutorial 02, one
tool call away.

### 4. Three question patterns that work

What follows are the tool-call sequences a well-prompted session converges
on. All JSON shown was captured from real calls against
`tests/fixtures/demo.c` (compiled as in Tutorial 01).

#### Pattern 1: "Is there a use-after-free risk in this module?"

Expected sequence: `load_program` → `functions` (map the alloc/free
surface) → `vfpath` malloc-ret → free-arg0 (what gets freed, where) →
`defuse` on the freed variable (any uses *after* the free?) → conclusion
quoting the witness. The full transcript is in step 5.

#### Pattern 2: "What can pointer X point to?"

Expected sequence: pick the anchor form for how "X" is identified —
`{"file", "line"[, "name"]}` when the user names a source line,
`{"func", "ret": true}` / `{"func", "arg": N}` when they name a function's
result or parameter (Tutorial 03's forms; on no-debug-info bitcode only
the `func` forms work, Tutorial 05) — then one `pts` call:

```json
// pts {"params": {"var": {"func": "malloc", "ret": true}}}
{"total": 1, "truncated": false, "vars": [
  {"var":       {"id": 14, "kind": "ValVar",
                 "ir": "ValVar ID: 14\n   %call = call noalias ptr @malloc(...) ...",
                 "loc": {"file": ".../demo.c", "func": "make_buf", "line": 4}},
   "points_to": [{"id": 15, "kind": "HeapObjVar",
                  "ir": "HeapObjVar ID: 15\n   %call = call noalias ptr @malloc(...) ...",
                  "loc": {"file": ".../demo.c", "func": "make_buf", "line": 4}}]}]}
```

Interpreting the kinds is the LLM's job and the `schema` `node_kinds`
list is its dictionary: `HeapObjVar` = "the heap allocation made at this
site" (one abstract object per malloc callsite), other `*ObjVar` kinds
mean stack/global/function objects. A sound answer to the user is "X may
point to the buffer allocated at demo.c:4 in make_buf" — *may*, because
Andersen's analysis is an over-approximation.

#### Pattern 3: "Who calls F, including through function pointers?"

One `callers` call; the per-callsite rows answer both halves:

```json
// callers {"params": {"func": "free"}}
{"calls": [{"callee": "free", "caller": "use_after_free", "direct": true,
            "callsite": {"kind": "CallICFGNode",
                         "ir": "...call void @free(ptr noundef %1)...",
                         "loc": {"file": ".../demo.c", "func": "use_after_free", "line": 10}}}],
 "function": "free", "matched_functions": 1, "total": 1, "truncated": false}
```

`direct: false` rows are the function-pointer half: SVF resolved an
indirect callsite to F via points-to (Tutorial 02's `indirect.c` shows
one). Two follow-ups the LLM should know: a surprising `total: 0` with
`matched_functions: 1` means "exists but never called — check for a
wrapper" (the `xmalloc` lesson, Tutorial 05); a misspelled name gets a
did-you-mean error with candidate names (Tutorial 02).

### 5. A sample session

*Illustrative transcript; tool outputs are real* (captured via the
in-memory MCP client on this machine, demo.c compiled with `-g`; long `ir`
strings and repeated records trimmed with `...`).

> **User:** Is there a use-after-free risk in `/tmp/demo.ll`?
>
> **Assistant:** I'll load the module and map its allocation surface first.
>
> ```json
> // load_program {"bitcode_paths": ["/tmp/demo.ll"]}
> {"functions": 9, "icfg_nodes": 93, "pag_nodes": 183, "svfg_nodes": 223,
>  "socket_path": "/tmp/svf-mcp-wh4fnga9/svf-301010.sock", "modules": ["/tmp/demo.ll"]}
>
> // functions {"params": {"pattern": "free|alloc"}}
> {"functions": [
>    {"name": "free",   "is_decl": true,  "num_args": 1, "loc": {"file": "", "line": 0}},
>    {"name": "malloc", "is_decl": true,  "num_args": 1, "loc": {"file": "", "line": 0}},
>    {"name": "use_after_free", "is_decl": false, "num_args": 0,
>     "loc": {"file": ".../demo.c", "line": 6}}],
>  "total": 3, "truncated": false}
> ```
>
> The module heap-allocates via `malloc` and frees via `free`. Let me trace
> what gets freed: value-flow from every malloc return to every `free`
> argument.
>
> ```json
> // vfpath {"params": {"source": {"func": "malloc", "ret": true},
> //                    "sink": {"func": "free", "arg": 0}, "k": 1}}
> {"paths": [{"length": 6, "steps": [
>    {"edge": null,               "node": {"kind": "AddrVFGNode",
>       "ir": "...%call = call noalias ptr @malloc(i64 noundef %conv)...",
>       "loc": {"file": ".../demo.c", "func": "make_buf", "line": 4}}},
>    {"edge": "IntraDirSVFGEdge", "node": {"kind": "IntraPHIVFGNode",  "loc": {"func": "make_buf", "line": 4, "...": "..."}}},
>    {"edge": "IntraDirSVFGEdge", "node": {"kind": "FormalRetVFGNode", "loc": {"func": "make_buf", "line": 4, "...": "..."}}},
>    {"edge": "RetDirSVFGEdge",   "node": {"kind": "ActualRetVFGNode", "loc": {"func": "use_after_free", "line": 8, "...": "..."}},
>     "callsite": {"kind": "CallICFGNode", "loc": {"func": "use_after_free", "line": 8, "...": "..."}}},
>    {"edge": "IntraDirSVFGEdge", "node": {"kind": "StoreVFGNode",
>       "ir": "...store ptr %call, ptr %b...",
>       "loc": {"func": "use_after_free", "line": 8, "...": "..."}}},
>    {"edge": "IntraIndSVFGEdge", "node": {"kind": "LoadVFGNode",
>       "ir": "...%1 = load ptr, ptr %b...",
>       "loc": {"func": "use_after_free", "line": 10, "...": "..."}}}]}],
>  "sources": 1, "sinks": 2, "truncated": false, "visited": 5}
> ```
>
> So the buffer allocated in `make_buf` (line 4) is returned to
> `use_after_free`, stored into `b` (line 8), and `b` is what gets freed at
> line 10. Now the decisive question: is `b` used *after* that free?
>
> ```json
> // defuse {"params": {"var": {"file": "demo.c", "line": 8, "name": "b"}}}
> {"total": 1, "truncated": false, "vars": [{
>   "var": {"kind": "ValVar", "ir": "ValVar ID: 44\n   %b = alloca ptr, align 8 ", "...": "..."},
>   "defs": [{"stmt": "Addr",  "at": {"loc": {"func": "use_after_free", "line": 0, "...": "..."}, "...": "..."}},
>            {"stmt": "Store", "at": {"loc": {"func": "use_after_free", "line": 8, "...": "..."}, "...": "..."}}],
>   "uses": [{"stmt": "Load", "at": {"loc": {"func": "use_after_free", "line": 9,  "...": "..."}, "...": "..."}},
>            {"stmt": "Load", "at": {"loc": {"func": "use_after_free", "line": 10, "...": "..."}, "...": "..."}},
>            {"stmt": "Load", "at": {"loc": {"func": "use_after_free", "line": 11, "...": "..."}, "...": "..."}}]}]}
> ```
>
> **Yes — there is a use-after-free risk in `use_after_free()`:**
>
> 1. **Allocation:** `make_buf` mallocs a buffer (demo.c:4) and returns it;
>    it is stored into `b` at demo.c:8 (witness path above, ending in the
>    load of `b` at line 10 that feeds `free`).
> 2. **Free:** `free(b)` at demo.c:10 (the line-10 `Load` use feeds it).
> 3. **Use after free:** `b` is loaded again at **demo.c:11**
>    (`return b[0]`) — after the line-10 free on the same straight-line path.
>
> Caveat: SVF reports MAY-paths — this is strong evidence with a concrete
> witness, not a proof of a feasible execution; the final read of the code
> (here trivially confirming lines 8–11 are sequential) is on us.

The shape to copy from this transcript: every claim in the conclusion
points back at an evidence record (a step in the witness path or a
def/use row), and the limits of the analysis are stated rather than
glossed over. (A session could go one step further and confirm with
`vfpath` malloc-ret → `{"file": "demo.c", "line": 11}` — Tutorial 04's
money shot — which returns the 6-step witness ending at the line-11 load.)

### 6. Troubleshooting

- **"no program loaded; call load_program first"** — every query tool
  returns this (as a structured `{"error": ...}`, never an exception)
  until `load_program` succeeds. Also appears after `unload_program`.
- **Daemon log** — stdout/stderr of the spawned daemon land in
  `daemon.log` next to the socket, inside a `svf-mcp-*` temp dir; the
  `socket_path` field of the `load_program` result tells you where. If
  the daemon dies before listening (e.g. malformed IR), `load_program`
  surfaces the log tail in its error automatically.
- **Load timeout** — `load_program` waits up to **600 s** for the daemon
  socket. demo-scale programs take ~1 s, bc.bc ~2 s, bash.bc ~57 s
  (Tutorial 05's table); if you hit 600 s the program is genuinely huge —
  there is no partial load.
- **"daemon unreachable ... call load_program again"** — the daemon died
  or its socket vanished; one `load_program` call recovers (it always
  replaces whatever was there).
- **Socket cleanup** — `unload_program`, a replacing `load_program`, or
  wrapper exit all shut the daemon down and remove the temp dir. Orphans,
  if any, are `svf-mcp-*` dirs under `$TMPDIR`; safe to delete.
- **Run on the real machine** — the registered command embeds absolute
  paths (the `py311-mcp` interpreter, the `Release-build` binary): the
  config is per-machine, not portable. Re-run `claude mcp add` (or edit
  `.mcp.json` placeholders) on each box.

## What you learned

- Two ways to register: `claude mcp add svf --env SVF_HARNESS_BIN=... --
  <python> server.py` (user scope) or a checked-in `.mcp.json` with
  hand-substituted placeholders (project scope).
- 13 tools; arguments go **nested under `"params"`** — the flat form
  silently drops them (a too-broad result is the symptom).
- Schema-first: `load_program`, then `schema` once — it is the
  authoritative contract the short docstrings defer to.
- Three reusable patterns: UAF triage (functions → vfpath → defuse),
  points-to ("may point to the object allocated at *site*"), and callers
  with `direct: false` for function-pointer calls.
- Good sessions ground every claim in evidence records and state the
  MAY-analysis caveat.

## Next

This is the last tutorial. From here:

- Run the harness on your own bitcode — Tutorial 05's no-debug-info and
  performance notes are the field guide.
- [`mcp/svf_harness_mcp/README.md`](../../mcp/svf_harness_mcp/README.md)
  for wrapper design details;
  [`svf-llvm/tools/Harness/README.md`](../../svf-llvm/tools/Harness/README.md)
  for the daemon/CLI reference.
- What's coming next for the harness lives in [`docs/FUTURE.md`](../FUTURE.md)
  and the epics in [`docs/PROGRESS.md`](../PROGRESS.md).
