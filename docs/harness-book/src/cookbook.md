# Query Cookbook

This chapter is the method-level reference. Each entry follows the same shape:
purpose, parameters, result, example, interpretation, and follow-ups. Run
`schema` in your checkout for the exact contract.

Examples assume `/tmp/demo.ll` was compiled from `demo.c` and that commands run
from the repository root.

## `schema`

### Purpose

Return the live API contract: methods, parameters, graph kinds, node kinds,
edge kinds, and evidence shape.

### Parameters

None.

### Result

Top-level metadata including `methods`, `node_kinds`, `edge_kinds`, and
program/config information.

### Example

```bash
Release-build/bin/svf-harness --oneshot schema /tmp/demo.ll > /tmp/schema.json
```

### Interpretation

Use this before relying on docs from another checkout. If a method is absent
from `schema`, it is not available in the running binary.

### Follow-ups

Use `analysis_config` to inspect active precision settings.

## `summary`

### Purpose

Check that the program loaded and see graph sizes.

### Parameters

None.

### Result

Counts for functions, ICFG nodes, PAG nodes, and SVFG nodes.

### Example

```bash
Release-build/bin/svf-harness --oneshot summary /tmp/demo.ll
```

### Interpretation

Unexpectedly small counts usually mean the wrong input was loaded. Larger
programs can have very large SVFGs; use daemon mode for repeated queries.

### Follow-ups

Use `functions` to resolve symbol names.

## `functions`

### Purpose

List functions visible to the module, optionally filtered by regex.

### Parameters

Optional `pattern`, an ECMAScript regex matched against function names.

### Result

`functions` array with `name`, `is_decl`, `num_args`, and `loc`; plus `total`
and `truncated`.

### Example

```bash
Release-build/bin/svf-harness --oneshot functions \
  --params '{"pattern":"malloc|free"}' /tmp/demo.ll
```

### Interpretation

Declarations have empty locations. If `truncated` is true, narrow `pattern`
before using the list as evidence.

### Follow-ups

Use exact names with `callers`, `callees`, and `cfg`.

## `callers`

### Purpose

Find callsites that may call an exact callee.

### Parameters

Required `func`, the exact function name.

### Result

Callsite records with caller/callee evidence and a truncation flag.

### Example

```bash
Release-build/bin/svf-harness --oneshot callers \
  --params '{"func":"free"}' /tmp/demo.ll
```

### Interpretation

For indirect calls, results are based on pointer-analysis resolution and should
be read as may-call evidence.

### Follow-ups

Use `callees` from a caller, or `node`/`neighbors` on callgraph nodes.

## `callees`

### Purpose

List callees a function may call, including indirect callees when resolved.

### Parameters

Required `func`, the exact function name.

### Result

Callee records with callsite evidence and direct/indirect edge information.

### Example

```bash
Release-build/bin/svf-harness --oneshot callees \
  --params '{"func":"use_after_free"}' /tmp/demo.ll
```

### Interpretation

An indirect callee list is an over-approximation. Missing debug locations on
external declarations are normal.

### Follow-ups

Use `cfg` for statement-level context inside a caller.

## `cfg`

### Purpose

Return the interprocedural-control-flow nodes and edges for one function.

### Parameters

Required `func`, the exact function name.

### Result

Nodes with evidence and CFG edges for the selected function.

### Example

```bash
Release-build/bin/svf-harness --oneshot cfg \
  --params '{"func":"use_after_free"}' /tmp/demo.ll
```

### Interpretation

CFG nodes are LLVM/SVF nodes, not source statements. Use `loc` and `ir` fields
to connect them back to source.

### Follow-ups

Use `defuse` or `vfpath` for value movement through the CFG.

## `defuse`

### Purpose

Find definitions and uses for variables selected by an anchor.

### Parameters

Required `var` anchor. Supported forms include `{file,line,name?}`,
`{func,ret:true}`, and `{func,arg:N}`.

### Result

Resolved anchor matches plus definition and use evidence.

### Example

```bash
Release-build/bin/svf-harness --oneshot defuse \
  --params '{"var":{"file":"demo.c","line":8,"name":"b"}}' /tmp/demo.ll
```

### Interpretation

Line anchors require debug info. Name filters match LLVM value-name substrings,
so compile with `-fno-discard-value-names` when possible.

### Follow-ups

Use `pts` on the same anchor for points-to objects.

## `pts`

### Purpose

Return Andersen points-to objects for variables selected by an anchor.

### Parameters

Required `var` anchor using `{file,line,name?}`, `{func,ret:true}`, or
`{func,arg:N}`.

### Result

Resolved variables and points-to targets with object evidence.

### Example

```bash
Release-build/bin/svf-harness --oneshot pts \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

### Interpretation

Points-to sets are may-sets. A target means the pointer may refer to that
object under the analysis abstraction.

### Follow-ups

Use `aliases` to find other variables that may name the same object.

## `aliases`

### Purpose

Find variables that may alias the selected variables under Andersen analysis.

### Parameters

Required `var` anchor.

### Result

Resolved variables and alias candidates with evidence.

### Example

```bash
Release-build/bin/svf-harness --oneshot aliases \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

### Interpretation

Aliases are may-aliases. Use source evidence and follow-up value-flow queries
before making bug claims.

### Follow-ups

Compare with `cfl_aliases` or `dda_aliases` for precision-sensitive questions.

## `cfl_pts`

### Purpose

Return CFLAlias points-to results for an anchor.

### Parameters

Required `var` anchor.

### Result

Resolved variables and CFL points-to objects.

### Example

```bash
Release-build/bin/svf-harness --oneshot cfl_pts \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

### Interpretation

CFLAlias can be more expensive than default Andersen. Use it for targeted
questions, not broad sweeps over large programs.

### Follow-ups

Compare with `pts` and inspect `analysis_config`.

## `cfl_aliases`

### Purpose

Return CFLAlias may-alias results for an anchor.

### Parameters

Required `var` anchor.

### Result

Resolved variables and CFL alias candidates.

### Example

```bash
Release-build/bin/svf-harness --oneshot cfl_aliases \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

### Interpretation

This query can be slow on larger modules. Prefer a specific anchor and keep
timeouts realistic.

### Follow-ups

Use `aliases` for a cheaper baseline and `dda_aliases` for demand-driven
comparison.

## `dda_pts`

### Purpose

Return demand-driven points-to results for an anchor.

### Parameters

Required `var` anchor.

### Result

Resolved variables and FlowDDA points-to objects.

### Example

```bash
Release-build/bin/svf-harness --oneshot dda_pts \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

### Interpretation

Demand-driven analysis is useful when one anchor matters more than global
coverage.

### Follow-ups

Compare with `pts` and `cfl_pts`.

## `dda_aliases`

### Purpose

Return demand-driven alias results for an anchor.

### Parameters

Required `var` anchor.

### Result

Resolved variables and FlowDDA alias candidates.

### Example

```bash
Release-build/bin/svf-harness --oneshot dda_aliases \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

### Interpretation

Read results as may-alias evidence for the selected demand query, not as a
whole-program proof of non-aliasing.

### Follow-ups

Use `vfpath` when the alias result affects a flow claim.

## `saber_leaks`

### Purpose

Run SABER leak summaries.

### Parameters

None.

### Result

Leak reports with source and allocation/free evidence when available.

### Example

```bash
Release-build/bin/svf-harness --oneshot saber_leaks /tmp/demo.ll
```

### Interpretation

Checker results are analysis findings. Inspect evidence and reproduce with
small fixtures before treating them as final bug reports.

### Follow-ups

Use `vfpath`, `pts`, and `callers` to explain a reported allocation lifecycle.

## `saber_double_frees`

### Purpose

Run SABER double-free summaries.

### Parameters

None.

### Result

Double-free reports with relevant free-site evidence when available.

### Example

```bash
Release-build/bin/svf-harness --oneshot saber_double_frees /tmp/demo.ll
```

### Interpretation

Absence of reports is not a proof of absence. It means this checker did not
find a report under the current abstraction.

### Follow-ups

Use `callers` for `free` and `vfpath` from allocation to free arguments.

## `saber_file_leaks`

### Purpose

Run SABER file-leak summaries.

### Parameters

None.

### Result

File-resource leak reports with evidence where available.

### Example

```bash
Release-build/bin/svf-harness --oneshot saber_file_leaks /tmp/demo.ll
```

### Interpretation

The query is most useful on programs using file APIs. Small heap-only fixtures
may return no reports.

### Follow-ups

Use `functions` for `fopen|fclose` and `callers` for resource operations.

## `mta_summary`

### Purpose

Summarize multithreaded-analysis state.

### Parameters

None.

### Result

Counts and high-level MTA facts for the loaded program.

### Example

```bash
Release-build/bin/svf-harness --oneshot mta_summary /tmp/thread_mhp.ll
```

### Interpretation

Use this first to see whether thread analysis found meaningful thread state.

### Follow-ups

Use `mta_mhp` on two source locations.

## `mta_mhp`

### Purpose

Ask whether two source locations may happen in parallel.

### Parameters

Required `left` and `right` source anchors: `{file,line,kind?}`.

### Result

A boolean may-happen-in-parallel answer plus matched anchor evidence.

### Example

```bash
Release-build/bin/svf-harness --oneshot mta_mhp \
  --params '{"left":{"file":"thread_mhp.c","line":6},
             "right":{"file":"thread_mhp.c","line":13}}' /tmp/thread_mhp.ll
```

### Interpretation

`true` is a MAY result. It says parallel execution is possible under the
analysis, not that a runtime schedule definitely reaches both points.

### Follow-ups

Use `cfg` and `callers` to inspect the thread entry and spawn sites.

## `ae_summary`

### Purpose

Summarize abstract-execution coverage and state availability.

### Parameters

None.

### Result

AE mode/config strings, analyzed-function counts, trace coverage, and abstract
state entry counts.

### Example

```bash
Release-build/bin/svf-harness --oneshot ae_summary /tmp/ae_state.ll
```

### Interpretation

Run this before asking for a state at a line. If coverage is empty, `ae_state`
will not have useful state to return.

### Follow-ups

Use `ae_state` on a source location with state.

## `ae_state`

### Purpose

Return abstract-execution state near a source location.

### Parameters

Required `at` source anchor `{file,line,kind?}` and optional `limit`.

### Result

Matched ICFG anchors and capped variable/address abstract values.

### Example

```bash
Release-build/bin/svf-harness --oneshot ae_state \
  --params '{"at":{"file":"ae_state.c","line":10},"limit":20}' /tmp/ae_state.ll
```

### Interpretation

The state is an abstract state, not a concrete execution trace. Use it to
inspect inferred value ranges or symbolic state at the matched program point.

### Follow-ups

Use `cfg` around the function and `ae_summary` for coverage context.

## `vfpath`

### Purpose

Find witness paths in the sparse value-flow graph from one source to one sink.

### Parameters

Required `source` and `sink` anchors. Optional `k`, `max_steps`, and
`max_visited`.

### Result

Path objects with steps, edge kinds, node evidence, truncation flags, and
unresolved-anchor diagnostics when applicable.

### Example

```bash
Release-build/bin/svf-harness --oneshot vfpath \
  --params '{"source":{"func":"malloc","ret":true},
             "sink":{"file":"demo.c","line":11},
             "k":1,"max_steps":80}' /tmp/demo.ll
```

### Interpretation

A witness path explains possible value movement. It is not a concrete runtime
trace unless the surrounding control-flow and feasibility conditions also hold.

### Follow-ups

Use `reachable` for many sinks, then return to `vfpath` for explanations.

## `reachable`

### Purpose

Check one source against up to 20 sink anchors.

### Parameters

Required `source` and `sinks`; optional `max_steps` and `max_visited`.

### Result

Per-sink reachability outcomes and optional path/truncation diagnostics.

### Example

```bash
Release-build/bin/svf-harness --oneshot reachable \
  --params '{"source":{"func":"malloc","ret":true},
             "sinks":[{"file":"demo.c","line":10},
                      {"file":"demo.c","line":11}]}' /tmp/demo.ll
```

### Interpretation

Use this for triage. It is cheaper to ask many yes/no questions first, then
explain the interesting ones with `vfpath`.

### Follow-ups

Use `vfpath` for reachable sinks.

## `graphs`

### Purpose

List graph surfaces available for browsing.

### Parameters

None.

### Result

Graph names and high-level metadata for surfaces such as `icfg`, `svfg`,
`svfir`, and `callgraph`.

### Example

```bash
Release-build/bin/svf-harness --oneshot graphs /tmp/demo.ll
```

### Interpretation

Use this to choose the graph argument for `graph_nodes`, `graph_edges`, `node`,
and `neighbors`.

### Follow-ups

Use `graph_nodes` with a graph and optional kind filter.

## `graph_nodes`

### Purpose

Page through nodes in one graph.

### Parameters

Required `graph`. Optional `kind`, `func`, `limit`, and `offset`.

### Result

Node records with graph id, kind, evidence, pagination fields, and truncation.

### Example

```bash
Release-build/bin/svf-harness --oneshot graph_nodes \
  --params '{"graph":"svfg","kind":"LoadVFGNode","limit":5}' /tmp/demo.ll
```

### Interpretation

Always check pagination fields before assuming you saw all matching nodes.

### Follow-ups

Use `node` for a selected id and `neighbors` for local graph context.

## `graph_edges`

### Purpose

Page through edges in one graph.

### Parameters

Required `graph`. Optional `kind`, `limit`, and `offset`.

### Result

Edge records with source id, destination id, kind, evidence when available, and
pagination fields.

### Example

```bash
Release-build/bin/svf-harness --oneshot graph_edges \
  --params '{"graph":"svfg","limit":5}' /tmp/demo.ll
```

### Interpretation

Edges often need `node` lookups on both endpoints before they are useful to a
human reader.

### Follow-ups

Use `node` and `neighbors` on edge endpoints.

## `node`

### Purpose

Fetch one graph node by id.

### Parameters

Required `graph` and integer `id`.

### Result

One node record with kind and evidence, or a not-found diagnostic.

### Example

```bash
Release-build/bin/svf-harness --oneshot node \
  --params '{"graph":"svfg","id":42}' /tmp/demo.ll
```

### Interpretation

Node ids are graph-local. An id from `svfg` is not meaningful in `icfg` unless
you explicitly query that graph.

### Follow-ups

Use `neighbors` to inspect adjacent flow or control edges.

## `neighbors`

### Purpose

Fetch adjacent nodes and edges around one graph node.

### Parameters

Required `graph` and `id`. Optional `direction`: `in`, `out`, or `both`.

### Result

Neighbor node and edge records around the selected node.

### Example

```bash
Release-build/bin/svf-harness --oneshot neighbors \
  --params '{"graph":"svfg","id":42,"direction":"both"}' /tmp/demo.ll
```

### Interpretation

Use neighborhoods to explain a path step or inspect why a value can move to
the next node.

### Follow-ups

Use `graph_edges` to find more edges of the same kind.

## `analysis_config`

### Purpose

Report active precision settings and supported analysis surfaces.

### Parameters

None.

### Result

Configuration details such as pointer-analysis defaults, SVFG mode, and
surface support status.

### Example

```bash
Release-build/bin/svf-harness --oneshot analysis_config /tmp/demo.ll
```

### Interpretation

Record this next to precision-sensitive claims. Two runs with different SVFG
modes or supported surfaces may not be directly comparable.

### Follow-ups

Use `schema` for method contracts and precision-specific queries such as
`cfl_pts` or `dda_pts`.
