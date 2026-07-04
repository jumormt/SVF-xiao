# Core Concepts

This chapter gives the mental model used by every tutorial and cookbook entry.

## Evidence Records

Most result objects include an evidence record:

```json
{
  "id": 68,
  "kind": "LoadVFGNode",
  "loc": {"file": ".../demo.c", "func": "use_after_free", "line": 11},
  "ir": "%2 = load ptr, ptr %b, align 8, !dbg !21"
}
```

Read it as:

- `id`: graph-local node id. Do not reuse it across graphs.
- `kind`: SVF node kind, such as `CallICFGNode` or `LoadVFGNode`.
- `loc`: best source location recovered from debug info.
- `ir`: truncated textual SVF/LLVM context.

Empty `loc.file` and `line: 0` usually mean the node has no source counterpart
in this module, such as an external declaration or synthetic object.

## Anchors

Many queries start from an anchor. The common variable-anchor forms are:

```json
{"file":"demo.c","line":8,"name":"b"}
{"func":"malloc","ret":true}
{"func":"free","arg":0}
```

Use `{file,line,name}` when source debug info is present. Use `{func,ret}` and
`{func,arg}` when you care about call return values or actual arguments across
call sites. Resolve function names with `functions` before writing exact-name
queries.

Source-location anchors for MTA and AE look like:

```json
{"file":"thread_mhp.c","line":6}
```

The file match is by path suffix, so `demo.c` can match
`svf-llvm/tools/Harness/tests/fixtures/demo.c`.

## MAY Analysis

SVF is mostly answering may questions:

- a function pointer may call these callees;
- a pointer may point to these objects;
- two variables may alias;
- a value may flow from this source to that sink;
- two program points may happen in parallel.

A positive result is useful evidence, not a concrete runtime trace. A negative
result means the current analysis did not find the relation under its model; it
is not a universal proof unless you also know the analysis assumptions.

## Query Cost

The cheap first queries are `summary`, `functions`, `schema`, `callers`,
`callees`, and focused `cfg` queries. Points-to and graph paging are usually
moderate on small programs. CFLAlias, FlowDDA, SABER, MTA, and AE are lazy
secondary surfaces; the first query on one of those surfaces may build extra
analysis state.

For larger programs:

- prefer daemon mode;
- start with `summary` and `analysis_config`;
- use regex filters and pagination;
- keep CFL/DDA queries narrowly anchored;
- use `reachable` for triage before asking for full `vfpath` witnesses.

## Truncation And Pagination

List-like results include `total` and `truncated`. Graph browsing also uses
`limit` and `offset`.

```json
{"total": 8, "limit": 2, "offset": 0, "truncated": true}
```

This means you saw only the first two rows. Do not treat the returned list as
complete. Increase `limit`, page with `offset`, or narrow the filter.

## The Schema Habit

When uncertain, run:

```bash
Release-build/bin/svf-harness --oneshot schema /tmp/demo.ll
```

Then check the method's parameters before changing the code or the docs. This
habit prevents most harness usage mistakes.
