# Tutorial 05 — A real-world program

## Goal

Leave the toy fixtures behind. Point the harness at GNU `bc` (a real
interpreter, 187 functions) straight from the SVF Test-Suite, and learn the
two things that change on real code: **what to expect when the bitcode has
no debug info** (empty `loc`s — and why you can still work), and **how to
recover when your first question is wrong** (the `xmalloc` lesson from
bash, 2368 functions). Plus honest performance numbers so you know what
"slow" means before you blame the harness.

Run the whole thing as a script:
[`examples/05-real-world.sh`](../../svf-llvm/tools/Harness/examples/05-real-world.sh)
(the bash part needs `RUN_BIG=1`, see step 6).

## Prerequisites

- Tutorials [01](01-getting-started.md)–[04](04-value-flow-witnesses.md):
  daemon basics, evidence records, anchors, witness paths.
- The SVF Test-Suite cloned at the repo root (the same clone that enables
  `ctest`): it ships prebuilt real-program bitcode under
  `Test-Suite/test_cases_bc/crux-bc/`, so there is nothing to compile.
  Without it, the script prints `SKIP: Test-Suite not cloned` and exits 0.

## Steps

### 1. Serve `bc.bc` — a real program loads in seconds

```bash
Release-build/bin/svf-harness serve \
    Test-Suite/test_cases_bc/crux-bc/bc.bc --socket /tmp/h.sock &
# wait for the socket, then:
Release-build/bin/svf-harness summary --socket /tmp/h.sock | python3 -m json.tool
```

Real output (the script times the load with `$SECONDS`):

```text
daemon ready in 1s (pid 298704)
```

```json
{
    "functions": 187,
    "icfg_nodes": 15567,
    "pag_nodes": 17132,
    "svfg_nodes": 29014
}
```

**Interpretation.** Two orders of magnitude bigger than demo.c
(9 functions / 223 SVFG nodes → 187 / 29,014), and the full
SVFIR → Andersen → SVFG pipeline still builds in a second or two. The cost
model from Tutorial 01 holds at scale: you pay once at `serve`; every query
afterwards answers from the prebuilt state in milliseconds (measured
numbers in step 7).

### 2. The no-debug-info reality

crux-bc bitcode was compiled **without `-g`**. Watch what that does to
`loc`:

```bash
Release-build/bin/svf-harness functions --params '{"pattern": "alloc|free"}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (abbreviated to 4 of the 10 hits):

```json
{
    "functions": [
        {"is_decl": false, "loc": {"file": "", "line": 0}, "name": "bc_free_num", "num_args": 1},
        {"is_decl": false, "loc": {"file": "", "line": 0}, "name": "bc_malloc",   "num_args": 1},
        {"is_decl": true,  "loc": {"file": "", "line": 0}, "name": "free",        "num_args": 1},
        {"is_decl": true,  "loc": {"file": "", "line": 0}, "name": "malloc",      "num_args": 1}
    ],
    "total": 10,
    "truncated": false
}
```

**Interpretation.** In Tutorial 01 an empty `loc` meant "external
declaration". Here **every** `loc` is empty — including `bc_malloc` and
`bc_free_num`, which are definitions (`is_decl: false`). Without debug
info LLVM IR simply has no file/line to report; this is a property of how
the bitcode was produced, not a harness failure. Compare the same kind of
evidence record (a `callers` callsite) from demo.c and from bc.bc:

```json
// demo.c (compiled with -g): loc is fully populated
{"caller": "use_after_free", "direct": true,
 "callsite": {"kind": "CallICFGNode",
              "ir": "CallICFGNode31 {fun: use_after_free{ \"ln\": 9, ... }}\n   call void @fill(ptr noundef %0) ...",
              "loc": {"file": ".../tests/fixtures/demo.c", "func": "use_after_free", "line": 9}}}

// bc.bc (no -g): file/line gone — but func and ir survive
{"caller": "open_new_file", "direct": true,
 "callsite": {"kind": "CallICFGNode",
              "ir": "CallICFGNode655 {fun: open_new_file}\n   call void @free(ptr noundef %18) #11 ...",
              "loc": {"file": "", "func": "open_new_file", "line": 0}}}
```

Two of the four evidence fields degrade; two don't:

- **`loc.file` / `loc.line`** — empty/0. Gone.
- **`loc.func`** — still there: the containing function comes from the IR
  structure, not from debug info.
- **`ir`** — still there in full: the actual instruction
  (`call void @free(ptr noundef %18)`) names the LLVM values involved,
  which you can grep in `llvm-dis` output if you need ground truth.

Practical consequences: navigate by **function name + IR text** instead of
file:line, and remember that the `{file, line}` *anchor form* from
Tutorial 03 is unusable on such bitcode — use the `{func, ret}` /
`{func, arg}` forms, which is exactly what the rest of this tutorial does.

### 3. `callers free` — the release surface of bc

```bash
Release-build/bin/svf-harness callers --params '{"func": "free"}' \
    --socket /tmp/h.sock
```

Real output, summarized by the script:

```text
ok: 35 callsites of free across 21 functions, all direct
    callers: addbyte arg_str bc_divide bc_free_num bc_out_num call_str
             clear_func fpop free_a_tree free_args lookup more_arrays
             more_functions more_variables open_new_file pop pop_vars
             rl_input set_genstr_size yyfree yyparse
```

**Interpretation.** 35 rows, one per callsite (the same per-callsite
expansion as Tutorial 02 — `yyparse` alone frees in many places), all
`direct: true`. This is your worklist for any free-related audit: every
row carries the callsite evidence record shown above, so an LLM (or you)
can jump straight to the instruction.

### 4. `vfpath` — heap lifecycle witnesses at scale

Source: every malloc return. Sink: the first argument of every `free`
call. Both ends are `{func, ...}` anchors — no line numbers needed:

```bash
Release-build/bin/svf-harness vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"func": "free", "arg": 0}, "k": 2}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (first path; the second is its twin for `num2`):

```json
{
    "paths": [
        {
            "length": 3,
            "steps": [
                {"edge": null,
                 "node": {"id": 1235, "kind": "AddrVFGNode",
                          "ir": "AddrVFGNode ID: 1235 AddrStmt: [Var14650 <-- Var14651]\t\nValVar ID: 14650\n   %call51 = call noalias ptr @malloc(i64 noundef %conv50) #11 ",
                          "loc": {"file": "", "func": "bc_divide", "line": 0}}},
                {"edge": "IntraDirSVFGEdge",
                 "node": {"id": 9824, "kind": "StoreVFGNode",
                          "ir": "StoreVFGNode ID: 9824 StoreStmt: [Var14445 <-- Var14650]\t\nValVar ID: 14652\n   store ptr %call51, ptr %num1, align 8 ",
                          "loc": {"file": "", "func": "bc_divide", "line": 0}}},
                {"edge": "IntraIndSVFGEdge",
                 "node": {"id": 7476, "kind": "LoadVFGNode",
                          "ir": "LoadVFGNode ID: 7476 LoadStmt: [Var15162 <-- Var14445]\t\nValVar ID: 15162\n   %245 = load ptr, ptr %num1, align 8 ",
                          "loc": {"file": "", "func": "bc_divide", "line": 0}}}
            ]
        },
        { "length": 3, "steps": [ "... same shape via %num2 ..." ] }
    ],
    "sinks": 70,
    "sources": 12,
    "truncated": false,
    "visited": 20
}
```

**Interpretation.** The anchors fanned out exactly as Tutorial 03
promised: 12 sources (bc has 12 malloc callsites) and 70 sink nodes (the
value-flow nodes feeding 35 free calls). The shortest witnesses live in
`bc_divide`, which mallocs two scratch numbers and frees them:
malloc → `store ... ptr %num1` → `load ptr %num1` → (that loaded value is
the `free` argument). Every node says *which function* it is in
(`loc.func`) and *which instruction* it is (`ir`) — a fully navigable
witness with zero debug info. As always, MAY-paths: this shows the
allocation **can** reach the free, i.e. the normal lifecycle; finding a
use **after** the free is the same query with a different sink, exactly
like Tutorial 04's demo.

### 5. `pts` — one heap object per malloc callsite

```bash
Release-build/bin/svf-harness pts --params '{"var": {"func": "malloc", "ret": true}}' \
    --socket /tmp/h.sock
```

Real output (1 of the 12 vars):

```json
{
    "total": 12,
    "truncated": false,
    "vars": [
        {"var":       {"id": 1147, "kind": "ValVar",
                       "ir": "ValVar ID: 1147\n   %call = call noalias ptr @malloc(i64 noundef %add12) #11 ",
                       "loc": {"file": "", "func": "yyparse", "line": 0}},
         "points_to": [{"id": 1148, "kind": "HeapObjVar",
                        "ir": "HeapObjVar ID: 1148\n   %call = call noalias ptr @malloc(i64 noundef %add12) #11 ",
                        "loc": {"file": "", "func": "yyparse", "line": 0}}]
    }, "..." ]
}
```

**Interpretation.** Same `HeapObjVar` story as Tutorial 03, now × 12: SVF
names heap objects by allocation site, so each malloc return points to
exactly its own object. On real programs this per-site naming is what
keeps points-to sets readable — `pts` of any pointer in bc comes back as
"the object allocated at *this* `ir` in *this* function".

### 6. The `xmalloc` lesson — when zero results means wrong question

Now the stretch program: `bash.bc` (2368 functions, 610,860 SVFG nodes).
The script runs this part only with `RUN_BIG=1` — the load takes about a
minute and ~2.7 GB of RAM:

```bash
RUN_BIG=1 bash svf-llvm/tools/Harness/examples/05-real-world.sh
```

You know bash uses the classic `xmalloc` wrapper, so you ask the obvious
question — and get a real-output surprise:

```json
// callers {"func": "xmalloc"}
{"calls": [], "function": "xmalloc", "matched_functions": 1, "total": 0, "truncated": false}
```

Zero callers. Is the harness broken? **Don't trust, verify — and don't
assume, search.** `matched_functions: 1` says the name resolved (no
did-you-mean error, the function exists); `total: 0` says nothing calls
it. The self-correction move is the one an LLM should always make after a
surprising zero — widen the question from an exact name to a pattern:

```json
// functions {"pattern": "xmalloc"}
{
    "functions": [
        {"is_decl": false, "loc": {"file": "", "line": 0}, "name": "sh_xmalloc", "num_args": 3},
        {"is_decl": false, "loc": {"file": "", "line": 0}, "name": "xmalloc",    "num_args": 1}
    ],
    "total": 2,
    "truncated": false
}
```

There it is: `sh_xmalloc`, with **3** arguments to xmalloc's 1. This bash
was built with allocation tracking — every call goes through `sh_xmalloc`,
which takes the size *plus a file string and line number*. Re-ask with the
right name:

```text
// callers {"func": "sh_xmalloc"}
ok: 603 callsites, 200 returned, truncated=true
```

603 callsites (capped at 200 rows, `truncated: true` — the cap contract
from Tutorial 02). The first row's evidence shows the tracking arguments
in the IR:

```json
{"caller": "set_shell_name", "direct": true,
 "callsite": {"kind": "CallICFGNode",
              "ir": "CallICFGNode5072 {fun: set_shell_name}\n   %call49 = call ptr @sh_xmalloc(i64 noundef %add, ptr noundef @.str.26, i32 noundef 1781) ...",
              "loc": {"file": "", "func": "set_shell_name", "line": 0}}}
```

And value-flow works unchanged on the 611k-node SVFG:

```text
// vfpath {"source": {"func": "sh_xmalloc", "ret": true}, "sink": {"func": "sh_xfree", "arg": 0}, "k": 1}
ok: witness path of length 3 (603 sources, 2182 sinks, visited 605)
```

> **Sidebar — verified against the IR.** The 0-callers result was
> cross-checked the hard way during the harness shakedown: `llvm-dis`
> on bash.bc confirms there is no `call ... @xmalloc` anywhere — `xmalloc`
> is defined but unreferenced in this build, and all allocation routes
> through `sh_xmalloc`. A surprising answer from the harness is a prompt
> to verify, not to distrust: the `ir` field of every evidence record
> gives you the exact text to grep for.

### 7. Performance expectations

Measured on this machine during the harness shakedown (your numbers will
vary with hardware, not in shape):

| Program | Functions | SVFG nodes | Load (`serve`) | RSS | Typical query |
|---------|-----------|------------|-------|-----|---------------|
| demo.c  | 9    | 223     | < 1 s | tiny    | ~ms |
| bc.bc   | 187  | 29,014  | 1–2 s | small   | ~30 ms (`vfpath` 26 ms, `pts` 24 ms) |
| bash.bc | 2368 | 610,860 | ~57 s | ~2.7 GB | 50–340 ms (`callers` 49 ms, `vfpath` 116 ms, `reachable` batch 340 ms) |

Two caveats worth knowing:

- **`aliases` is the outlier**: ~7.1 s on bash.bc. Its v0 implementation
  is quadratic in the candidate set (a known limitation, queued for
  rework) — on big programs prefer `pts` (compare points-to sets
  yourself) or scope alias questions tightly.
- **Load is once, queries are cheap.** 57 s feels long, but it buys you
  thousands of sub-second queries; never restart the daemon between
  questions.

## What you learned

- Real-program bitcode straight from Test-Suite loads in seconds (bc) to
  about a minute (bash); the pay-once-query-forever cost model holds.
- Without `-g`, `loc.file`/`loc.line` are empty — but `loc.func` and `ir`
  still anchor every result, and the `{func, ret}`/`{func, arg}` anchor
  forms keep all variable queries usable.
- A surprising `total: 0` with `matched_functions: 1` means "exists,
  uncalled" — widen to a `functions` pattern, find the real wrapper
  (`sh_xmalloc`), re-ask. Verify surprises against the `ir` evidence.
- Caps and truncation flags (200 rows, `truncated: true`) are how big
  programs stay readable; perf outlier to remember: v0 `aliases`.

## Next

[Tutorial 06 — Claude Code via MCP](06-claude-code-mcp.md): wire the
harness into Claude Code so an LLM runs these exact loops —
load → schema → question → evidence-grounded answer — by itself.
