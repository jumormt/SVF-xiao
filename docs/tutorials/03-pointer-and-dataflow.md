# Tutorial 03 — Pointer and dataflow queries

## Goal

Ask the three pointer/dataflow questions — `pts` (what does this pointer
point to?), `aliases` (what else may name the same memory?), and `defuse`
(where is this variable defined and used?) — and master the **var anchor**,
the little JSON object all three use to say *which* variable you mean.
You will also make a deliberate mistake and see how the error guides you
back.

Run the whole thing as a script:
[`examples/03-pointer-dataflow.sh`](../../svf-llvm/tools/Harness/examples/03-pointer-dataflow.sh).

## Prerequisites

[Tutorial 02](02-exploring-a-program.md) completed — daemon serving
`/tmp/demo.ll` on `/tmp/h.sock`. If you shut the daemon down at the end of
the previous tutorial, restart it: `Release-build/bin/svf-harness serve /tmp/demo.ll --socket /tmp/h.sock &`

## The anchor mental model

SVF variables are IR-level entities; you don't know their ids. An anchor
describes one (or several) of them in source terms. There are exactly three
forms — use exactly ONE shape per query:

| Form | Example | Means |
|------|---------|-------|
| `{file, line[, name]}` | `{"file": "demo.c", "line": 8, "name": "b"}` | all values *defined* at that source line, optionally filtered by LLVM value-name substring |
| `{func, ret: true}` | `{"func": "malloc", "ret": true}` | the return value of `func`, at every callsite of it |
| `{func, arg: N}` | `{"func": "free", "arg": 0}` | the N-th actual argument, at every callsite of `func` |

The two `func` forms shine for *external* functions like `malloc`/`free`:
they have no body, but every callsite produces concrete values you can
anchor on. One anchor may resolve to several vars (a function called from
many sites) — results are always a `vars` list.

## Steps

### 1. `pts` — what does the malloc return value point to?

```bash
Release-build/bin/svf-harness pts \
    --params '{"var": {"func": "malloc", "ret": true}}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (paths abbreviated):

```json
{
    "total": 1,
    "truncated": false,
    "vars": [
        {
            "points_to": [
                {
                    "id": 15,
                    "ir": "HeapObjVar ID: 15\n   %call = call noalias ptr @malloc(i64 noundef %conv) #4 ... { \"ln\": 4, ... }",
                    "kind": "HeapObjVar",
                    "loc": {"file": ".../tests/fixtures/demo.c", "func": "make_buf", "line": 4}
                }
            ],
            "var": {
                "id": 14,
                "ir": "ValVar ID: 14\n   %call = call noalias ptr @malloc(i64 noundef %conv) #4 ... { \"ln\": 4, ... }",
                "kind": "ValVar",
                "loc": {"file": ".../tests/fixtures/demo.c", "func": "make_buf", "line": 4}
            }
        }
    ]
}
```

**Interpretation.** The anchor resolved to one var: `ValVar 14`, the
`%call` result of the `malloc` call inside `make_buf` (demo.c:4). Its
points-to set contains one object: **`HeapObjVar 15`**. A `HeapObjVar` is
the *abstract heap object* SVF creates per allocation site — not one runtime
allocation but the summary of **every** allocation that line ever performs.
Its `loc` is the allocation site itself (line 4). When two pointers'
points-to sets share a `HeapObjVar` id, they may point into the same memory
— that id is the currency all pointer reasoning trades in.

### 2. `aliases` — what else may name that memory?

```bash
Release-build/bin/svf-harness aliases \
    --params '{"var": {"func": "malloc", "ret": true}}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (paths abbreviated):

```json
{
    "total": 1,
    "truncated": false,
    "vars": [
        {
            "aliases": [
                {
                    "id": 6,
                    "ir": "RetValPN ID: 6 unique return node for function make_buf",
                    "kind": "RetValPN",
                    "loc": {"file": ".../tests/fixtures/demo.c", "func": "make_buf", "line": 4}
                }
            ],
            "var": {
                "id": 14,
                "ir": "ValVar ID: 14\n   %call = call noalias ptr @malloc(...) ...",
                "kind": "ValVar",
                "loc": {"file": ".../tests/fixtures/demo.c", "func": "make_buf", "line": 4}
            }
        }
    ]
}
```

**Interpretation.** One alias: `RetValPN 6`, `make_buf`'s "unique return
node" — the synthetic var that carries whatever `make_buf` returns. It
aliases the malloc result because the function returns it directly. Two
things to internalize:

- **May-alias semantics.** An alias pair means the two vars' points-to sets
  intersect — they *may* refer to the same memory on some execution. It is
  evidence to inspect, not proof they ever do at runtime. The flip side is
  stronger: vars *not* listed are guaranteed (up to analysis soundness) not
  to alias.
- **v0 scope.** Candidates are top-level `ValVar`s of the queried var's
  *own function* only — that's why `b` in `use_after_free` is absent here.
  To reason across functions, compare `pts` results instead: `b`'s
  points-to set also contains `HeapObjVar 15`, same id, same memory.

### 3. `defuse` — where is `b` defined and used?

Anchor form one, with a name filter (this is what
`-fno-discard-value-names` bought you):

```bash
Release-build/bin/svf-harness defuse \
    --params '{"var": {"file": "demo.c", "line": 8, "name": "b"}}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output, trimmed (each `at` is a full evidence record):

```json
{
    "total": 1,
    "truncated": false,
    "vars": [
        {
            "defs": [
                {"stmt": "Addr",
                 "at": {"kind": "IntraICFGNode", "id": 26,
                        "ir": "IntraICFGNode26 {fun: use_after_free}\nAddrStmt: [Var44 <-- Var45]\t\nValVar ID: 44\n   %b = alloca ptr, align 8 ",
                        "loc": {"file": "", "func": "use_after_free", "line": 0}}},
                {"stmt": "Store",
                 "at": {"kind": "IntraICFGNode", "id": 29,
                        "ir": "IntraICFGNode29 {...}\nStoreStmt: [Var44 <-- Var46]\t\nValVar ID: 48\n   store ptr %c…",
                        "loc": {"file": ".../demo.c", "func": "use_after_free", "line": 8}}}
            ],
            "uses": [
                {"stmt": "Load", "at": {"kind": "IntraICFGNode", "loc": {"line": 9,  ...}, ...}},
                {"stmt": "Load", "at": {"kind": "IntraICFGNode", "loc": {"line": 10, ...}, ...}},
                {"stmt": "Load", "at": {"kind": "IntraICFGNode", "loc": {"line": 11, ...}, ...}}
            ],
            "var": {
                "id": 44,
                "ir": "ValVar ID: 44\n   %b = alloca ptr, align 8 ",
                "kind": "ValVar",
                "loc": {"file": "", "func": "use_after_free", "line": 0}
            }
        }
    ]
}
```

**Interpretation.** The anchor resolved to `%b`, the stack slot. Its defs:
the `Addr` statement (the `alloca` itself — empty loc, prologue placement,
as seen in Tutorial 02) and the `Store` at line 8 (`b = make_buf(8)`). Its
uses: three `Load`s at **lines 9, 10, 11** — `fill(b)`, `free(b)`, and
`b[0]`. That last pair is the use-after-free in def/use form: a use at
line 11 after the pointer was handed to `free` at line 10. `defuse` is the
quick triage tool; Tutorial 04 turns this into an end-to-end witness path.

### 4. The third anchor form — `{func, arg}`

What exactly does `free` free? Anchor on its 0th argument:

```bash
Release-build/bin/svf-harness pts \
    --params '{"var": {"func": "free", "arg": 0}}' \
    --socket /tmp/h.sock | python3 -m json.tool
```

Real output (paths abbreviated):

```json
{
    "total": 1,
    "truncated": false,
    "vars": [
        {
            "points_to": [
                {
                    "id": 15,
                    "ir": "HeapObjVar ID: 15\n   %call = call noalias ptr @malloc(...) ... { \"ln\": 4, ... }",
                    "kind": "HeapObjVar",
                    "loc": {"file": ".../tests/fixtures/demo.c", "func": "make_buf", "line": 4}
                }
            ],
            "var": {
                "id": 51,
                "ir": "ValVar ID: 51\n   %1 = load ptr, ptr %b, align 8 ... { \"ln\": 10, ... }",
                "kind": "ValVar",
                "loc": {"file": ".../tests/fixtures/demo.c", "func": "use_after_free", "line": 10}
            }
        }
    ]
}
```

**Interpretation.** The anchor resolved to `ValVar 51` — the value loaded
from `b` at line 10 and passed to `free`. Its points-to set is
`{HeapObjVar 15}`: **the same id** the malloc return pointed to in step 1.
That closes the loop without any path query: what is freed at line 10 is
exactly the heap object allocated at line 4. This anchor form is the idiom
for sink-side questions ("what reaches this argument of `memcpy` /
`system` / `free`?").

### 5. A deliberate mistake — and the way back

Misspell the function name:

```bash
Release-build/bin/svf-harness pts \
    --params '{"var": {"func": "make_buff", "ret": true}}' \
    --socket /tmp/h.sock
```

Real output (exit code 1):

```json
{
    "code": -32000,
    "message": "unknown function 'make_buff'; did you mean: make_buf, main, malloc, free, long_ir?"
}
```

**Interpretation.** The error contract: the CLI prints the JSON-RPC error
object and exits 1 — stdout is still pure JSON, so a script (or an LLM)
can parse failures the same way as successes. And the message is
*actionable*: the harness ranks known function names by edit distance and
suggests the closest ones — `make_buf` first. Anchor failures behave the
same way: a bad line number gets you the accepted anchor forms plus the
nearest lines that *do* define values (you will see one in Tutorial 04).
The design rule: every error should tell you what to try next.

## What you learned

- The three anchor forms: `{file, line[, name]}`, `{func, ret}`,
  `{func, arg}` — and that `func` forms work through *callsites*, so they
  apply to body-less externals like `malloc`/`free`.
- `HeapObjVar` = abstract allocation site; shared ids across `pts` results
  are how you connect pointers to the same memory.
- `aliases` is may-alias, scoped (v0) to the var's own function; use `pts`
  id comparison for cross-function reasoning.
- `defuse` lists defs/uses as `{stmt, at}` with full evidence records.
- Errors are structured JSON with did-you-mean / accepted-forms hints,
  exit code 1.

## Next

[Tutorial 04 — Value-flow witnesses](04-value-flow-witnesses.md): `vfpath`
and `reachable` — turning "line 11 uses freed memory" into a step-by-step
witness path with evidence at every hop.
