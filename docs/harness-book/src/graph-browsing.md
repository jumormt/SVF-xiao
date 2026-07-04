# Graph Browsing

Question: how do I inspect the raw SVF graph nodes and edges behind higher
level query results?

Fixture: `/tmp/demo.ll` from `demo.c`.

## 1. List Available Graphs

```bash
Release-build/bin/svf-harness --oneshot graphs /tmp/demo.ll
```

Typical graph names are:

- `icfg`: interprocedural control-flow graph;
- `svfg`: sparse value-flow graph;
- `svfir`: program assignment graph / SVFIR;
- `callgraph`: call graph.

Use these names as the `graph` parameter in browsing queries.

## 2. Page Through Nodes

```bash
Release-build/bin/svf-harness --oneshot graph_nodes \
  --params '{"graph":"svfg","kind":"LoadVFGNode","limit":2}' /tmp/demo.ll
```

Representative output:

```json
{
  "graph": "svfg",
  "nodes": [
    {"id":64,"kind":"LoadVFGNode","loc":{"func":"make_buf","line":4}},
    {"id":65,"kind":"LoadVFGNode","loc":{"func":"fill","line":5}}
  ],
  "total": 8,
  "limit": 2,
  "offset": 0,
  "truncated": true
}
```

Because `truncated` is true, this is not the full list. Page with `offset` or
narrow by `func` when available.

## 3. Page Through Edges

```bash
Release-build/bin/svf-harness --oneshot graph_edges \
  --params '{"graph":"svfg","limit":5}' /tmp/demo.ll
```

Edges are most useful when paired with endpoint lookups. The edge kind tells
you whether flow is direct, indirect through memory, call/return related, and
so on.

## 4. Inspect One Node

```bash
Release-build/bin/svf-harness --oneshot node \
  --params '{"graph":"svfg","id":68}' /tmp/demo.ll
```

Use the `id` from `vfpath` steps or `graph_nodes` results. Remember that ids
are graph-local.

## 5. Inspect Local Neighborhood

```bash
Release-build/bin/svf-harness --oneshot neighbors \
  --params '{"graph":"svfg","id":68,"direction":"both"}' /tmp/demo.ll
```

Neighborhoods answer "why could the path enter or leave this node?" They are
better for explanations than dumping an entire graph.

## Common Pitfalls

- Node ids are not stable across different graph types.
- Pagination is mandatory on large graphs.
- Graph browsing exposes SVF internals; prefer high-level queries first, then
  browse when you need to explain a specific node or edge.

## Check Yourself

- Find the `LoadVFGNode` at `demo.c:11`.
- Fetch that node by id.
- Ask for incoming neighbors only.

Next: use checker summaries when you want bug-oriented entry points.
