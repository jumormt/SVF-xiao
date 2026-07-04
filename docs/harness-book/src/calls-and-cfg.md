# Calls And CFG

Question: who calls what, including indirect calls, and where are the relevant
statements inside one function?

Fixtures: `/tmp/demo.ll` from `demo.c` and `/tmp/indirect.ll` from
`indirect.c`.

## 1. Direct Callees In `use_after_free`

```bash
Release-build/bin/svf-harness --oneshot callees \
  --params '{"func":"use_after_free"}' /tmp/demo.ll
```

Representative output:

```json
{
  "function": "use_after_free",
  "calls": [
    {"caller":"use_after_free","callee":"make_buf","direct":true},
    {"caller":"use_after_free","callee":"fill","direct":true},
    {"caller":"use_after_free","callee":"free","direct":true}
  ],
  "truncated": false
}
```

This answers what the function may call. For direct calls in this fixture, the
answer matches source expectations.

## 2. Callers Of A Library Function

```bash
Release-build/bin/svf-harness --oneshot callers \
  --params '{"func":"free"}' /tmp/demo.ll
```

Use `callers` when you start from a resource API or library function and need
to find the release surface. The callsite evidence points back to
`demo.c:10`.

## 3. Indirect Callees

Compile the indirect fixture:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/indirect.ll svf-llvm/tools/Harness/tests/fixtures/indirect.c
```

Ask for callees of the function-pointer dispatch:

```bash
Release-build/bin/svf-harness --oneshot callees \
  --params '{"func":"apply"}' /tmp/indirect.ll
```

Representative output:

```json
{
  "function": "apply",
  "calls": [
    {"caller":"apply","callee":"dbl","direct":false,
     "callsite":{"loc":{"file":".../indirect.c","line":15}}},
    {"caller":"apply","callee":"neg","direct":false,
     "callsite":{"loc":{"file":".../indirect.c","line":15}}}
  ],
  "total": 2,
  "truncated": false
}
```

`direct: false` is the important field. Andersen resolved the function pointer
call at line 15 to two possible callees. This is a may-call set, not proof that
both happen in one execution.

## 4. Statement-Level Context With `cfg`

```bash
Release-build/bin/svf-harness --oneshot cfg \
  --params '{"func":"use_after_free"}' /tmp/demo.ll
```

Look for nodes around:

- line 8: call to `make_buf`;
- line 9: call to `fill`;
- line 10: call to `free`;
- line 11: load from `b`.

CFG nodes are LLVM/SVF control-flow nodes, so one source line can produce
several nodes. Use `kind`, `loc`, and `ir` together instead of relying on line
number alone.

## Common Pitfalls

- `callers` and `callees` require exact function names. Use `functions` first.
- Indirect call targets are may targets.
- CFG output can be long. Filter mentally by `loc.func`, `loc.line`, and
  `kind`.

## Check Yourself

- Which function calls `fill`?
- Which call in `indirect.c` is indirect?
- Which source line contains the `free` call in `use_after_free`?

Next: anchor variables at those lines and ask pointer/dataflow questions.
