# Bug Checkers

Question: how do I use SABER checker summaries without overclaiming what they
prove?

Fixtures: `/tmp/demo.ll` for command shape; Test-Suite SABER bitcode when you
need positive examples.

## 1. Run Leak Summaries

```bash
Release-build/bin/svf-harness --oneshot saber_leaks /tmp/demo.ll
```

Representative output on `demo.c`:

```json
{
  "checker": "leak",
  "sources": 1,
  "sinks": 1,
  "bugs": [],
  "total": 0,
  "truncated": false
}
```

No leak is expected here because the fixture frees the allocation. That does
not make the program safe; it still has a use-after-free pattern explored in
the value-flow chapter.

## 2. Run Double-Free And File-Leak Summaries

```bash
Release-build/bin/svf-harness --oneshot saber_double_frees /tmp/demo.ll
Release-build/bin/svf-harness --oneshot saber_file_leaks /tmp/demo.ll
```

Use these as bug-oriented entry points. When a report appears, inspect the
reported evidence and then ask lower-level queries to explain it.

## 3. Explain A Checker Report

For a leak report, a typical follow-up sequence is:

```bash
Release-build/bin/svf-harness --oneshot functions \
  --params '{"pattern":"malloc|free|open|close"}' /path/to/program.bc

Release-build/bin/svf-harness --oneshot callers \
  --params '{"func":"free"}' /path/to/program.bc

Release-build/bin/svf-harness --oneshot vfpath \
  --params '{"source":{"func":"malloc","ret":true},
             "sink":{"func":"free","arg":0},
             "k":2}' /path/to/program.bc
```

This turns a checker summary into evidence a human can inspect.

## Common Pitfalls

- Checker output is not a replacement for evidence review.
- Absence of reports is not a proof of absence.
- Some fixtures are intentionally not positive examples for every checker.
- Real programs without debug info may still produce useful IR evidence but
  empty source locations.

## Check Yourself

- Why does `demo.c` have zero leak reports?
- Which follow-up query would explain how an allocation reaches `free`?
- Which method would you run before looking for `fopen`/`fclose` bugs?

Next: inspect thread and abstract-execution surfaces.
