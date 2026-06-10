# Tutorial 02 — Exploring a program

## Goal

Map out an unfamiliar program with five queries: `schema` (what can I ask?),
`functions` with a regex, `callers`/`callees` for call-graph navigation —
including an *indirect* call through a function-pointer table — and `cfg`
for a statement-level view of one function. You will also meet the evidence
record (`kind`/`id`/`loc`/`ir`) that every returned graph node carries.

Run the whole thing as a script:
[`examples/02-exploring.sh`](../../svf-llvm/tools/Harness/examples/02-exploring.sh).

## Prerequisites

[Tutorial 01](01-getting-started.md) completed — you have a daemon serving
`/tmp/demo.ll` on `/tmp/h.sock`. We will also compile a second fixture,
`indirect.c`, along the way.

## Steps

### 1. `schema` — ask the tool what it can do

Before anything else, ask the harness to describe itself. The full output is
large (it documents every method, all 67 node kinds, all 10 edge kinds, and
the evidence-record contract), so extract the headline:

```bash
Release-build/bin/svf-harness schema --socket /tmp/h.sock | python3 -c '
import json, sys
j = json.load(sys.stdin)
print("methods:   ", ", ".join(m["name"] for m in j["methods"]))
print("node_kinds:", len(j["node_kinds"]), "documented evidence kinds")
print("edge_kinds:", len(j["edge_kinds"]), "documented edge kinds")
'
```

Real output:

```
methods:    schema, summary, functions, callers, callees, cfg, defuse, pts, aliases, vfpath, reachable
node_kinds: 67 documented evidence kinds
edge_kinds: 10 documented edge kinds
```

**Interpretation.** `schema` is the authoritative contract — params, return
shapes, semantics, caps for every method, plus a description of every node
kind you might see in a result. When a later query returns a node of kind
`FormalRetVFGNode` and you wonder what that means, the answer is in
`schema().node_kinds`, not in any external document. An LLM agent (or you)
should fetch it once per session and treat it as ground truth.

### 2. `functions` with a regex

```bash
Release-build/bin/svf-harness functions --params '{"pattern": ".*free.*"}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (paths abbreviated):

```json
{
    "functions": [
        {
            "is_decl": true,
            "loc": {"file": "", "line": 0},
            "name": "free",
            "num_args": 1
        },
        {
            "is_decl": false,
            "loc": {"file": ".../tests/fixtures/demo.c", "line": 6},
            "name": "use_after_free",
            "num_args": 0
        }
    ],
    "total": 2,
    "truncated": false
}
```

**Interpretation.** The pattern is an ECMAScript regex with *search*
semantics (matched anywhere in the name), so `.*free.*` and `free` find the
same two functions. This is the standard first move before
`callers`/`callees`/`cfg`: resolve the exact symbol name and check
`is_decl` — you can ask for callers of a declaration like `free`, but it
has no body, hence no CFG and no callees.

### 3. `callers` — who calls `fill`?

```bash
Release-build/bin/svf-harness callers --params '{"func": "fill"}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (paths abbreviated):

```json
{
    "calls": [
        {
            "callee": "fill",
            "caller": "use_after_free",
            "callsite": {
                "id": 31,
                "ir": "CallICFGNode31 {fun: use_after_free{ \"ln\": 9, ... }}\n   call void @fill(ptr noundef %0), !dbg !23 CallICFGNode: { …",
                "kind": "CallICFGNode",
                "loc": {
                    "file": ".../tests/fixtures/demo.c",
                    "func": "use_after_free",
                    "line": 9
                }
            },
            "direct": true
        }
    ],
    "function": "fill",
    "matched_functions": 1,
    "total": 1,
    "truncated": false
}
```

**Interpretation.** Each call edge comes with a `callsite` — your first
**evidence record**. Every graph node any method returns has this same
shape: `kind` (one of the 67 `node_kinds`), `id` (stable only within this
daemon instance — don't persist it), `loc` (source mapping), and `ir` (SVF's
textual dump of the node, truncated to ~200 bytes with a trailing `…` when
longer; that ellipsis in the output above is the harness's own truncation
marker, not ours). The record is the answer to "says who?" — `loc` tells
you *where* in the source, `ir` shows the exact IR statement the claim is
about.

### 4. `callees` — what does `use_after_free` call?

```bash
Release-build/bin/svf-harness callees --params '{"func": "use_after_free"}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output, trimmed to the essentials (one row per callee; each carries a
full callsite evidence record like above):

```json
{
    "calls": [
        {"callee": "make_buf", "caller": "use_after_free", "direct": true,
         "callsite": {"kind": "CallICFGNode", "loc": {"line": 8, ...}, ...}},
        {"callee": "fill",     "caller": "use_after_free", "direct": true,
         "callsite": {"kind": "CallICFGNode", "loc": {"line": 9, ...}, ...}},
        {"callee": "free",     "caller": "use_after_free", "direct": true,
         "callsite": {"kind": "CallICFGNode", "loc": {"line": 10, ...}, ...}}
    ],
    "function": "use_after_free",
    "matched_functions": 1,
    "total": 3,
    "truncated": false
}
```

**Interpretation.** Lines 8/9/10 — exactly the three calls in the source.
All are `direct: true`: the callee is named at the callsite. The interesting
case is when it *isn't*:

### 5. Indirect calls — `callees` of `apply` in `indirect.c`

`tests/fixtures/indirect.c` dispatches through a function-pointer table:

```c
typedef int (*op_t)(int);
static int dbl(int x) { return x * 2; }
static int neg(int x) { return -x; }
int apply(int which, int x)
{
    op_t ops[2];
    ops[0] = dbl;
    ops[1] = neg;
    return ops[which](x);
}
```

Compile it and query in **oneshot mode** — no daemon, one query, exit
(handy for scripting and CI):

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o /tmp/indirect.ll svf-llvm/tools/Harness/tests/fixtures/indirect.c
Release-build/bin/svf-harness --oneshot callees --params '{"func": "apply"}' \
    /tmp/indirect.ll | python3 -m json.tool
```

Real output (paths abbreviated):

```json
{
    "calls": [
        {
            "callee": "dbl",
            "caller": "apply",
            "callsite": {
                "id": 23,
                "ir": "CallICFGNode23 {fun: apply{ \"ln\": 15, ... }}\n   %call = call i32 %1(i32 noundef %2), !dbg !34 CallICFGNode: {…",
                "kind": "CallICFGNode",
                "loc": {"file": ".../tests/fixtures/indirect.c", "func": "apply", "line": 15}
            },
            "direct": false
        },
        {
            "callee": "neg",
            "caller": "apply",
            "callsite": {
                "id": 23,
                "ir": "CallICFGNode23 {fun: apply{ \"ln\": 15, ... }}\n   %call = call i32 %1(i32 noundef %2), !dbg !34 CallICFGNode: {…",
                "kind": "CallICFGNode",
                "loc": {"file": ".../tests/fixtures/indirect.c", "func": "apply", "line": 15}
            },
            "direct": false
        }
    ],
    "function": "apply",
    "matched_functions": 1,
    "total": 2,
    "truncated": false
}
```

**Interpretation.** This is something `grep` cannot tell you. The callsite
IR is `call i32 %1(...)` — a call through a *value*, no callee name in
sight. Andersen's points-to analysis traced the stores `ops[0] = dbl;
ops[1] = neg` into the table and resolved the call to **both** `dbl` and
`neg`, flagged `direct: false`. Note both rows share callsite `id: 23` —
one callsite, two possible targets. That "both" is may-analysis semantics:
at runtime, `which` selects one of them; the analysis soundly reports every
function that *may* be called.

### 6. `cfg` — statement-level control flow of one function

Back on the demo daemon:

```bash
Release-build/bin/svf-harness cfg --params '{"func": "use_after_free"}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output, trimmed (17 nodes / 17 edges total; shown: the shape, the
first edges, and three representative nodes):

```json
{
    "edges": [
        {"dst": 26, "kind": "IntraCFGEdge", "src": 5},
        {"dst": 44, "kind": "RetCFGEdge",   "src": 6},
        {"dst": 27, "kind": "IntraCFGEdge", "src": 26},
        {"dst": 1,  "kind": "CallCFGEdge",  "src": 27},
        ...
    ],
    "function": "use_after_free",
    "nodes": [
        {
            "id": 5,
            "ir": "FunEntryICFGNode5 {fun: use_after_free{ \"ln\": 6, ... }}",
            "kind": "FunEntryICFGNode",
            "loc": {"file": ".../tests/fixtures/demo.c", "func": "use_after_free", "line": 6}
        },
        {
            "id": 26,
            "ir": "IntraICFGNode26 {fun: use_after_free}\nAddrStmt: [Var44 <-- Var45]\t\nValVar ID: 44\n   %b = alloca ptr, align 8 ",
            "kind": "IntraICFGNode",
            "loc": {"file": "", "func": "use_after_free", "line": 0}
        },
        {
            "id": 27,
            "ir": "CallICFGNode27 {fun: use_after_free{ \"ln\": 8, ... }}\n   %call = call ptr @make_buf(i32 noundef 8), !dbg !21 CallI…",
            "kind": "CallICFGNode",
            "loc": {"file": ".../tests/fixtures/demo.c", "func": "use_after_free", "line": 8}
        },
        ...
    ],
    "total_edges": 17,
    "total_nodes": 17,
    "truncated": false
}
```

**Interpretation.** The CFG is at IR-instruction granularity, not basic
blocks. Patterns to recognize:

- `FunEntryICFGNode`/`FunExitICFGNode` bracket the function.
- Every call is **split into a pair**: a `CallICFGNode` (the call goes out,
  via a `CallCFGEdge` into the callee) and a `RetICFGNode` (control comes
  back). That is why one source line like line 8 contributes several nodes.
- Node 26 (`%b = alloca ptr`) has an **empty `loc`** — stack-slot
  allocations are placed in the function prologue by clang and carry no
  debug location. Empty loc again means "no source counterpart", and the
  `ir` field still tells you exactly what it is.
- Edge kinds: `IntraCFGEdge` (normal flow), `CallCFGEdge`/`RetCFGEdge`
  (interprocedural, which is why some `dst` ids — like `1`, `make_buf`'s
  entry — live outside this function).

`cfg` caps at 500 nodes / 1000 edges (`truncated: true` beyond that).

## What you learned

- `schema` is the self-describing contract — fetch it first, trust it over
  any document.
- `functions(pattern)` → resolve names; `callers`/`callees` → walk the call
  graph with per-callsite evidence.
- `direct: false` rows are Andersen-resolved indirect calls — may-analysis
  reports *every* possible target.
- The evidence record `{kind, id, loc, ir}` is uniform everywhere; `ir`
  self-truncates at ~200 bytes with `…`.
- `--oneshot` runs a single query without a daemon.

## Next

[Tutorial 03 — Pointer and dataflow queries](03-pointer-and-dataflow.md):
`pts`, `aliases`, `defuse`, the three var-anchor forms, and how the harness
helps you recover from mistakes.
