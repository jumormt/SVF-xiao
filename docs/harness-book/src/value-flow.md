# Value Flow

Question: can the heap object returned by `malloc` flow to the load after
`free`, and what witness explains that relation?

Fixture: `/tmp/demo.ll` from `demo.c`.

## 1. Ask For A Witness Path

```bash
Release-build/bin/svf-harness --oneshot vfpath \
  --params '{"source":{"func":"malloc","ret":true},
             "sink":{"file":"demo.c","line":11},
             "k":1,
             "max_steps":80}' /tmp/demo.ll
```

Representative output:

```json
{
  "sources": 1,
  "sinks": 4,
  "paths": [
    {
      "length": 6,
      "steps": [
        {"edge": null, "node": {"kind":"AddrVFGNode", "loc":{"line":4}}},
        {"edge": "IntraDirSVFGEdge", "node": {"kind":"FormalRetVFGNode"}},
        {"edge": "RetDirSVFGEdge", "node": {"kind":"ActualRetVFGNode",
         "loc":{"line":8}}},
        {"edge": "IntraIndSVFGEdge", "node": {"kind":"LoadVFGNode",
         "loc":{"line":11}}}
      ]
    }
  ],
  "truncated": false
}
```

The real path includes six steps; this snippet shows the important landmarks:
allocation, return from `make_buf`, actual return at the callsite, and the load
from `b` at line 11.

## 2. Interpret The Witness

The path explains possible value movement in the sparse value-flow graph:

- line 4: `malloc` creates the heap object;
- line 8: `make_buf(8)` returns it into `b`;
- line 11: the pointer stored in `b` is loaded for `b[0]`.

The path does not by itself prove the source program has a feasible runtime
use-after-free. For that claim, combine this witness with control-flow context
showing `free(b)` occurs before the load.

## 3. Triage Several Sinks With `reachable`

```bash
Release-build/bin/svf-harness --oneshot reachable \
  --params '{"source":{"func":"malloc","ret":true},
             "sinks":[{"file":"demo.c","line":9},
                      {"file":"demo.c","line":10},
                      {"file":"demo.c","line":11}]}' /tmp/demo.ll
```

Use `reachable` when you have many candidate sinks. It answers reachability
first; use `vfpath` only on the interesting sinks.

## 4. Control Path Length

`vfpath` supports `max_steps`. When a path exceeds the cap, the harness keeps
the beginning and end and inserts an elision marker. This is useful for long
chain fixtures and real programs where a full path is too large for a prompt.

```bash
Release-build/bin/svf-harness --oneshot vfpath \
  --params '{"source":{"func":"malloc","ret":true},
             "sink":{"file":"demo.c","line":11},
             "max_steps":10}' /tmp/demo.ll
```

## Common Pitfalls

- A value-flow path is not a concrete execution trace.
- A bare source line can resolve to several sink nodes, which is why the output
  may say `sinks` is greater than one.
- If no path is found, first check anchor resolution before concluding the
  relation is absent.

## Check Yourself

- Which step crosses from `make_buf` back to `use_after_free`?
- Why does the sink at line 11 resolve to several nodes?
- When would you prefer `reachable` over `vfpath`?

Next: inspect graph nodes and neighborhoods behind a path step.
