# Pointers And Def-Use

Question: what memory can a pointer name, what else may alias it, and where is
the selected value defined or used?

Fixture: `/tmp/demo.ll` from `demo.c`.

## 1. Points-To From A Return Anchor

The allocation source is the return value of `malloc`, called inside
`make_buf` at line 4.

```bash
Release-build/bin/svf-harness --oneshot pts \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

Representative output:

```json
{
  "vars": [
    {
      "var": {"kind":"ValVar","loc":{"func":"make_buf","line":4}},
      "points_to": [
        {"kind":"HeapObjVar","loc":{"func":"make_buf","line":4}}
      ]
    }
  ],
  "total": 1,
  "truncated": false
}
```

Read this as: the selected return value may point to the heap object allocated
at the `malloc` callsite. The object evidence is the important bridge from
pointer value to allocation site.

## 2. Aliases For The Same Anchor

```bash
Release-build/bin/svf-harness --oneshot aliases \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

Aliases are may-aliases. They tell you which variables may name overlapping
objects under the current pointer analysis. They do not prove simultaneous
runtime aliasing.

## 3. Def-Use From A Source-Line Anchor

```bash
Release-build/bin/svf-harness --oneshot defuse \
  --params '{"var":{"file":"demo.c","line":8,"name":"b"}}' /tmp/demo.ll
```

Line 8 defines local `b` from `make_buf(8)`. The optional `name` filter keeps
the anchor focused on the LLVM value name. Without debug info or value names,
source-line anchors become less precise.

## 4. Argument Anchors

The third common anchor form selects actual arguments at callsites:

```bash
Release-build/bin/svf-harness --oneshot pts \
  --params '{"var":{"func":"free","arg":0}}' /tmp/demo.ll
```

This asks what the argument passed to `free` may point to. It is often the
right anchor when auditing release APIs.

## 5. Recover From A Bad Anchor

If you ask for a misspelled function:

```bash
Release-build/bin/svf-harness --oneshot pts \
  --params '{"var":{"func":"make_buff","ret":true}}' /tmp/demo.ll
```

Expect an unresolved-anchor diagnostic. Do not patch around it. Go back to
`functions` and resolve the exact name.

## Common Pitfalls

- Points-to and alias answers are may results.
- `{file,line,name}` depends on debug info and retained value names.
- `{func,ret}` selects return values at callsites of `func`, not the formal
  return node of the function body.
- `{func,arg}` uses zero-based argument indexes.

## Check Yourself

- Which heap object does `malloc` return point to?
- Which anchor would you use for the pointer passed to `free`?
- Why might `{file:"demo.c",line:8,name:"b"}` fail on optimized IR?

Next: compare pointer-analysis surfaces and then follow value flow.
