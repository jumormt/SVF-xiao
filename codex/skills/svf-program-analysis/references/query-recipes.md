# SVF Harness Query Recipes

Use these recipes to map natural-language program-analysis requests to
`svf-harness` methods. Always prefer a targeted query over a graph dump.

## Anchors

Variable/value anchors:

```json
{"file":"demo.c","line":8}
{"file":"demo.c","line":8,"name":"b"}
{"func":"malloc","ret":true}
{"func":"memcpy","arg":0}
```

ICFG source-location anchors for MTA/AE:

```json
{"file":"thread.c","line":12}
{"file":"thread.c","line":12,"kind":"CallICFGNode"}
```

## Common Questions

### What is loaded?

Run `summary`, `analysis_config`, and optionally `graphs`.

### What functions exist?

Run `functions` with a regex:

```json
{"pattern":"alloc|free|main"}
```

### Who calls this function?

Run `functions` first if the exact symbol is uncertain, then `callers`:

```json
{"func":"free"}
```

### What does this function call?

Use `callees`. Indirect calls resolved by Andersen are included with
`direct:false`.

### What statements define/use this variable?

Use `defuse`:

```json
{"var":{"file":"demo.c","line":8,"name":"b"}}
```

### What can this pointer point to?

Start with Andersen:

```json
{"var":{"func":"malloc","ret":true}}
```

Use `pts`; compare with `cfl_pts` or `dda_pts` when the user asks about
precision, flow sensitivity, demand-driven results, or disagreement.

### Which pointers may alias?

Use `aliases`, `cfl_aliases`, or `dda_aliases`. The current alias candidate
scope is same-function ValVars, so use `vfpath`/`pts` for interprocedural
evidence.

### Is there a value-flow path from source to sink?

Use `vfpath` for witness paths:

```json
{
  "source":{"func":"malloc","ret":true},
  "sink":{"file":"demo.c","line":11},
  "k":3,
  "max_visited":100000
}
```

Use `reachable` when screening multiple sinks:

```json
{
  "source":{"func":"malloc","ret":true},
  "sinks":[{"file":"demo.c","line":11},{"file":"demo.c","line":22}]
}
```

### Show local graph context around a node

Use `graph_nodes` to find a node, then `neighbors`:

```json
{"graph":"svfg","kind":"LoadVFGNode","func":"main","limit":20}
{"graph":"svfg","id":68,"direction":"both"}
```

### Does this program leak or double-free memory?

Use SABER summaries:

- `saber_leaks`
- `saber_double_frees`
- `saber_file_leaks`

Report bug type, function, source location, and event summary. Emphasize that
these are static checker findings.

### Could two source locations run in parallel?

Use `mta_summary` first to confirm threads/fork sites, then `mta_mhp`:

```json
{
  "left":{"file":"thread_mhp.c","line":6},
  "right":{"file":"thread_mhp.c","line":12}
}
```

### What does AE know at this line?

Use `ae_summary` first, then `ae_state`:

```json
{"at":{"file":"ae_state.c","line":10},"limit":50}
```

Report `has_state`, `vars_total`, `addrs_total`, and the most relevant
`{id,value,var}` rows. The `value` string is AE's abstract domain rendering.

## Interpreting Results

- `loc.file` may be empty for bitcode without debug info.
- `line:0` usually means no source-level location was available.
- `kind` tells what a node represents: `CallICFGNode`, `LoadVFGNode`,
  `HeapObjVar`, `ActualRetVFGNode`, etc.
- `vfpath` is a MAY value-flow witness through SVFG/memory SSA, not a proof of
  concrete exploitability.
- `pts`/alias/DDA/CFL answers are conservative static-analysis results.
