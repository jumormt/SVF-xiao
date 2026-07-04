# Threads And Abstract Execution

Question: how do I ask thread may-happen-in-parallel questions and inspect
abstract-execution state at a source location?

Fixtures: `/tmp/thread_mhp.ll` from `thread_mhp.c` and `/tmp/ae_state.ll` from
`ae_state.c`.

## 1. Compile Thread And AE Fixtures

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/thread_mhp.ll svf-llvm/tools/Harness/tests/fixtures/thread_mhp.c

clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/ae_state.ll svf-llvm/tools/Harness/tests/fixtures/ae_state.c
```

`thread_mhp.c` writes `shared` in a worker thread at line 6 and in `main` at
line 13 before `pthread_join`.

## 2. Summarize MTA

```bash
Release-build/bin/svf-harness --oneshot mta_summary /tmp/thread_mhp.ll
```

Use the summary to check whether the thread surface found useful state before
asking pairwise questions.

## 3. Ask May-Happen-In-Parallel

```bash
Release-build/bin/svf-harness --oneshot mta_mhp \
  --params '{"left":{"file":"thread_mhp.c","line":6},
             "right":{"file":"thread_mhp.c","line":13}}' /tmp/thread_mhp.ll
```

Representative output:

```json
{
  "analysis": "mta",
  "left_matches": 1,
  "right_matches": 1,
  "may_happen_in_parallel": true,
  "pairs_checked": 1,
  "witnesses": [
    {"left":{"loc":{"func":"worker","line":6}},
     "right":{"loc":{"func":"main","line":13}},
     "same_thread": false}
  ]
}
```

Read `true` as a MAY result: the analysis found that the two points may happen
in parallel. It is enough to prioritize a race investigation, not a complete
race proof.

## 4. Summarize Abstract Execution

```bash
Release-build/bin/svf-harness --oneshot ae_summary /tmp/ae_state.ll
```

Run this first when `ae_state` returns no useful state. It tells you whether AE
ran and how much state it recorded.

## 5. Inspect AE State At A Line

```bash
Release-build/bin/svf-harness --oneshot ae_state \
  --params '{"at":{"file":"ae_state.c","line":10},"limit":5}' /tmp/ae_state.ll
```

Representative output:

```json
{
  "analysis": "ae",
  "matches": 4,
  "states": [
    {
      "has_state": true,
      "node": {"kind":"CallICFGNode","loc":{"func":"main","line":10}},
      "state": {
        "vars_total": 17,
        "addrs_total": 2,
        "truncated": true
      }
    }
  ]
}
```

The `limit` caps displayed variables and addresses. If `truncated` is true,
increase the limit only after deciding the extra state is necessary.

## Common Pitfalls

- MTA answers may-happen-in-parallel, not must-happen-in-parallel.
- AE state is abstract state, not a concrete trace.
- Source-line anchors can match multiple ICFG nodes on the same line.
- MTA and AE are lazy surfaces; first query may cost more than later ones in
  the same daemon session.

## Check Yourself

- Why can line 10 in `ae_state.c` produce several AE matches?
- What does `same_thread: false` mean in an MTA witness?
- Which query should you run before a specific `ae_state` lookup?

Next: use the cookbook when you need method-level recall.
