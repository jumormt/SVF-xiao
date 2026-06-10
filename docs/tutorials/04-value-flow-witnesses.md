# Tutorial 04 — Value-flow witnesses

## Goal

Use the two value-flow methods: `vfpath`, which returns a step-by-step
**witness path** showing how a value travels from a source to a sink, and
`reachable`, which answers yes/no for a batch of sinks at once. You will
read a witness path edge by edge, see how unreachable and unresolvable
sinks are reported, and learn the truncation contract for long paths.

Run the whole thing as a script:
[`examples/04-value-flow.sh`](../../svf-llvm/tools/Harness/examples/04-value-flow.sh).

## Prerequisites

[Tutorial 03](03-pointer-and-dataflow.md) completed — daemon serving
`/tmp/demo.ll` on `/tmp/h.sock`. If you shut the daemon down at the end of
the previous tutorial, restart it: `Release-build/bin/svf-harness serve /tmp/demo.ll --socket /tmp/h.sock &`
The elision step also compiles `tests/fixtures/chain.c`.

## Steps

### 1. `vfpath` — the use-after-free witness

Source: the malloc return value. Sink: demo.c line 11 (`return b[0]`).
Both ends are var anchors from Tutorial 03.

```bash
Release-build/bin/svf-harness vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"file": "demo.c", "line": 11}, "k": 1}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (paths abbreviated, `ir` strings trimmed):

```json
{
    "paths": [
        {
            "length": 6,
            "steps": [
                {
                    "edge": null,
                    "node": {"id": 27, "kind": "AddrVFGNode",
                             "ir": "AddrVFGNode ID: 27 AddrStmt: [Var14 <-- Var15]\t\nValVar ID: 14\n   %call = call noalias ptr @malloc(...) ...",
                             "loc": {"file": ".../demo.c", "func": "make_buf", "line": 4}}
                },
                {
                    "edge": "IntraDirSVFGEdge",
                    "node": {"id": 155, "kind": "IntraPHIVFGNode",
                             "ir": "IntraPHIVFGNode ID: 155 SVFVar: [6 = PHI(14, )]\t ...",
                             "loc": {"file": ".../demo.c", "func": "make_buf", "line": 4}}
                },
                {
                    "edge": "IntraDirSVFGEdge",
                    "node": {"id": 154, "kind": "FormalRetVFGNode",
                             "ir": "FormalRetVFGNode ID: 154 Fun[make_buf]RetValPN ID: 6 unique return node for function make_buf",
                             "loc": {"file": ".../demo.c", "func": "make_buf", "line": 4}}
                },
                {
                    "edge": "RetDirSVFGEdge",
                    "callsite": {"id": 27, "kind": "CallICFGNode",
                                 "ir": "CallICFGNode27 {fun: use_after_free{ \"ln\": 8, ... }}\n   %call = call ptr @make_buf(i32 noundef 8), ...",
                                 "loc": {"file": ".../demo.c", "func": "use_after_free", "line": 8}},
                    "node": {"id": 127, "kind": "ActualRetVFGNode",
                             "ir": "ActualRetVFGNode ID: 127 CS[CallICFGNode: { \"ln\": 8, ... }]ValVar ID: 46\n   %call = call ptr @make_buf(...) ...",
                             "loc": {"file": ".../demo.c", "func": "use_after_free", "line": 8}}
                },
                {
                    "edge": "IntraDirSVFGEdge",
                    "node": {"id": 75, "kind": "StoreVFGNode",
                             "ir": "StoreVFGNode ID: 75 StoreStmt: [Var44 <-- Var46]\t\nValVar ID: 48\n   store ptr %call, ptr %b, ... { \"ln\": 8, ... }",
                             "loc": {"file": ".../demo.c", "func": "use_after_free", "line": 8}}
                },
                {
                    "edge": "IntraIndSVFGEdge",
                    "node": {"id": 68, "kind": "LoadVFGNode",
                             "ir": "LoadVFGNode ID: 68 LoadStmt: [Var55 <-- Var44]\t\nValVar ID: 55\n   %2 = load ptr, ptr %b, ... { \"ln\": 11, ... }",
                             "loc": {"file": ".../demo.c", "func": "use_after_free", "line": 11}}
                }
            ]
        }
    ],
    "sinks": 4,
    "sources": 1,
    "truncated": false,
    "visited": 5
}
```

**Interpretation — read the path like a story.** Each step is
`{edge, node}`: the edge is *how the value moved* from the previous step's
node; the first step's edge is `null` (it is the source).

1. **`AddrVFGNode` @ line 4** — the allocation. The malloc result (`Var14`)
   takes the address of the heap object (`Var15`, the `HeapObjVar` from
   Tutorial 03). Value-flow sources for heap bugs are almost always
   `AddrVFGNode`s.
2. **`IntraPHIVFGNode` → `FormalRetVFGNode` @ line 4** — inside `make_buf`,
   the value merges into the function's unique return node. Plumbing, but
   honest plumbing: this is how the value gets *to* the `return`.
3. **`RetDirSVFGEdge` → `ActualRetVFGNode` @ line 8** — the crucial hop:
   the value is **returned across the `make_buf` call boundary** back into
   the caller. Interprocedural steps carry an extra `callsite` evidence
   record telling you *which* callsite (line 8 in `use_after_free`) the
   return flows through — with `k` callsites you'd know which one is on the
   path. The edge-kind family is symmetric: `CallDirSVFGEdge` would mean
   "passed in as an argument", and the `*IndSVFGEdge` variants mean the
   same boundaries crossed by memory (not by a direct value).
4. **`StoreVFGNode` @ line 8** — the returned pointer is stored into `b`.
5. **`IntraIndSVFGEdge` → `LoadVFGNode` @ line 11** — *indirect* flow: the
   value travels through memory (the `b` slot) rather than through an SSA
   register, courtesy of SVF's memory SSA. The load at line 11 is our sink:
   `return b[0]` reads the pointer that was freed at line 10.

The counters: `sources: 1` and `sinks: 4` — the line-11 anchor expanded to
4 value-flow nodes (the load of `b`, the GEP, the byte load, the int
conversion); the BFS visited 5 nodes and found the shortest path to one
sink. `k` asks for up to k paths, **at most one (shortest) per distinct
sink node** — it will not enumerate alternative routes to the same sink.
And as always: this is a MAY-path — evidence to read, not a proven
execution trace.

### 2. `reachable` — triage a batch of sinks

One BFS, many sinks. Deliberately include a reachable sink (line 11), an
unreachable one (line 22, inside `long_ir_helper` — its values never come
from malloc), and an unresolvable one (line 999 does not exist):

```bash
Release-build/bin/svf-harness reachable --params \
    '{"source": {"func": "malloc", "ret": true},
      "sinks": [{"file": "demo.c", "line": 11},
                {"file": "demo.c", "line": 22},
                {"file": "demo.c", "line": 999}]}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output, with the (identical to step 1) witness path elided:

```json
{
    "results": [
        {
            "first_path": {"length": 6, "steps": [ ... same 6 steps as above ... ]},
            "reachable": true,
            "sink": {"file": "demo.c", "line": 11}
        },
        {
            "reachable": false,
            "sink": {"file": "demo.c", "line": 22}
        },
        {
            "error": "no value-flow nodes at file demo.c line 999; nearest lines with value-flow nodes: 13 18 22 24 26. accepted var anchor forms (use exactly ONE shape): {\"file\": \"demo.c\", \"line\": 8} — all values defined at that source line (add \"name\": \"b\" to filter by LLVM value-name substring); {\"func\": \"malloc\", \"ret\": true} — the return value at every callsite of func; {\"func\": \"memcpy\", \"arg\": 0} — the actual argument at every callsite of func",
            "reachable": false,
            "sink": {"file": "demo.c", "line": 999}
        }
    ],
    "sources": 1,
    "truncated": false,
    "visited": 29
}
```

**Interpretation.** Three different answers, three different meanings:

- **Row 1** — reachable, and `first_path` hands you the witness for free.
- **Row 2** — `reachable: false` with **no** `error` key: the sink anchor
  resolved fine; the value simply never flows there. This is the analysis
  saying "no", not failing.
- **Row 3** — `reachable: false` **with** an `error`: the anchor itself
  could not be resolved. Crucially, a bad sink does *not* abort the batch —
  every other row is still answered. The error message is the anchor-hint
  contract from Tutorial 03 in action: nearest defining lines
  (13 18 22 24 26) plus the three accepted forms. Always distinguish these
  two falses: one is a result, the other is a malformed question.

Batches are capped at 20 sinks (a larger list is rejected with an error
naming the cap).

### 3. Long paths — `max_steps` elision on chain.c

`tests/fixtures/chain.c` threads a malloc'd pointer through ten hop
functions, each with a local store/load — the witness path has 96 steps.
Nobody (human or LLM) wants 96 steps; ask for at most 10. Oneshot mode,
since it's a different program:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o /tmp/chain.ll svf-llvm/tools/Harness/tests/fixtures/chain.c
Release-build/bin/svf-harness --oneshot vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"file": "chain.c", "line": 25}, "k": 1, "max_steps": 10}' \
    /tmp/chain.ll | python3 -m json.tool
```

Real output, trimmed (the 10 shown steps are full evidence records like
before):

```json
{
    "paths": [
        {
            "length": 96,
            "steps": [
                {"edge": null,              "node": {"kind": "AddrVFGNode",      "loc": {"func": "alloc", "line": 11, ...}, ...}},
                {"edge": "IntraDirSVFGEdge","node": {"kind": "IntraPHIVFGNode",  "loc": {"func": "alloc", "line": 11, ...}, ...}},
                {"edge": "IntraDirSVFGEdge","node": {"kind": "FormalRetVFGNode", "loc": {"func": "alloc", "line": 11, ...}, ...}},
                {"edge": "RetDirSVFGEdge",  "node": {"kind": "ActualRetVFGNode", "loc": {"func": "main",  "line": 24, ...}, ...},
                                            "callsite": {"kind": "CallICFGNode", "loc": {"func": "main",  "line": 24, ...}, ...}},
                {"edge": "IntraDirSVFGEdge","node": {"kind": "ActualParmVFGNode","loc": {"func": "main",  "line": 24, ...}, ...}},
                {"elided_steps": 86},
                {"edge": "IntraDirSVFGEdge","node": {"kind": "IntraPHIVFGNode",  "loc": {"func": "hop9",  "line": 21, ...}, ...}},
                {"edge": "IntraDirSVFGEdge","node": {"kind": "FormalRetVFGNode", "loc": {"func": "hop9",  "line": 21, ...}, ...}},
                {"edge": "RetDirSVFGEdge",  "node": {"kind": "ActualRetVFGNode", "loc": {"func": "main",  "line": 24, ...}, ...},
                                            "callsite": {"kind": "CallICFGNode", "loc": {"func": "main",  "line": 24, ...}, ...}},
                {"edge": "IntraDirSVFGEdge","node": {"kind": "StoreVFGNode",     "loc": {"func": "main",  "line": 24, ...}, ...}},
                {"edge": "IntraIndSVFGEdge","node": {"kind": "LoadVFGNode",      "loc": {"func": "main",  "line": 25, ...}, ...}}
            ],
            "steps_truncated": true
        }
    ],
    "sinks": 3,
    "sources": 1,
    "truncated": false,
    "visited": 115
}
```

**Interpretation — the truncation contract.** `length: 96` is the true
path length; `steps` contains the first 5 and last 5 steps with a single
`{"elided_steps": 86}` marker where the middle was cut. The two ends are
the part that matters — where the value was born (the malloc in `alloc`,
chain.c:11) and how it dies (stored to `q`, loaded and dereferenced at
line 25); the middle is ten rounds of identical hop plumbing.
`steps_truncated: true` on the path tells you elision happened; note this
is *different* from the top-level `truncated`, which reports BFS budget
exhaustion (`max_visited`), not display elision. `max_steps` accepts 10 to
500 (500 is also the default cap for untruncated display). If you need the
middle after all, re-run with a larger `max_steps`, or split the query:
anchor an intermediate hop as the sink, then as the source.

## What you learned

- `vfpath` returns witness paths: steps of `{edge, node[, callsite]}`
  evidence; `edge` says how the value moved (`RetDirSVFGEdge` = returned
  across a call boundary, `CallDirSVFGEdge` = passed as argument,
  `*Ind*` = through memory SSA).
- One shortest path per distinct sink node; results are MAY-paths.
- `reachable` triages up to 20 sinks in one BFS; per-sink `error` rows
  distinguish "anchor didn't resolve" from a genuine `reachable: false`.
- Long paths self-elide: `length` is true length, `{elided_steps: N}`
  marks the cut, ends are preserved, `steps_truncated` flags it.

## Next

Tutorial 05 (real-world programs) is coming with Phase 2 of the tutorials
plan. Until then: point the harness at your own bitcode — everything you
just did works the same, only slower to load and richer in results. For
using the harness from Claude Code via MCP, see
[`mcp/svf_harness_mcp/README.md`](../../mcp/svf_harness_mcp/README.md).
