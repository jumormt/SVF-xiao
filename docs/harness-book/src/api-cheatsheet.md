# API Cheatsheet

Use this page when you already know the workflow and need the shortest reminder.
Use [Query Cookbook](cookbook.md) for method-level detail.

## Method Groups

| Category | Methods | First use |
|---|---|---|
| Contract and inventory | `schema`, `summary`, `functions`, `analysis_config` | Confirm loaded program, names, and active config. |
| Calls and control | `callers`, `callees`, `cfg` | Map call relationships and one-function control flow. |
| Variables and pointers | `defuse`, `pts`, `aliases` | Resolve anchors, points-to objects, and may-aliases. |
| Precision surfaces | `cfl_pts`, `cfl_aliases`, `dda_pts`, `dda_aliases` | Compare Andersen with CFLAlias and FlowDDA. |
| Value flow | `vfpath`, `reachable` | Explain source-to-sink flow or triage many sinks. |
| Graph browsing | `graphs`, `graph_nodes`, `graph_edges`, `node`, `neighbors` | Inspect raw SVF graph context. |
| Bug checkers | `saber_leaks`, `saber_double_frees`, `saber_file_leaks` | Start from SABER resource reports. |
| Threads and AE | `mta_summary`, `mta_mhp`, `ae_summary`, `ae_state` | Inspect MHP and abstract-execution state. |

## CLI Forms

Daemon mode:

```bash
Release-build/bin/svf-harness serve /tmp/demo.ll --socket /tmp/h.sock &
Release-build/bin/svf-harness functions \
  --params '{"pattern":"free"}' --socket /tmp/h.sock
Release-build/bin/svf-harness shutdown --socket /tmp/h.sock
```

Oneshot mode:

```bash
Release-build/bin/svf-harness --oneshot functions \
  --params '{"pattern":"free"}' /tmp/demo.ll
```

## MCP Forms

MCP program lifecycle:

```json
{"bitcode_paths":["/tmp/demo.ll"]}
```

Query tools use nested `params`:

```json
{"params":{"pattern":"free"}}
```

Do not use the flat query form:

```json
{"pattern":"free"}
```

The MCP framework accepts unknown top-level keys, but the wrapper ignores them.

## Anchor Forms

Variable anchors:

```json
{"file":"demo.c","line":8,"name":"b"}
{"func":"malloc","ret":true}
{"func":"free","arg":0}
```

Source-location anchors:

```json
{"file":"thread_mhp.c","line":6}
{"file":"ae_state.c","line":10,"kind":"CallICFGNode"}
```

## Claim Checklist

Before reporting an analysis conclusion, capture:

- bitcode path and compile flags;
- `analysis_config`;
- exact query JSON;
- evidence `kind`, `id`, `loc`, and relevant `ir`;
- truncation and pagination status;
- MAY-analysis caveat where applicable.
