# Deferred Features

Items intentionally out of the v0 thin slice, with trigger conditions.

| Feature | Source | Trigger to start |
|---------|--------|------------------|
| Declarative query language L_Q (Datalog-style, validation + repair hints) | Proposal Task 2.2 | Thin slice in daily use; fixed method set becomes limiting |
| Composable precision space C = D×K×F×H, region policies, Galois boundaries | Proposal Task 1 | Need beyond-Andersen precision in harness queries |
| Evidence with path conditions + abstract-state traces | Proposal Task 2.3 | Path-sensitive analysis integrated |
| Cost-aware profiling/contracts per region | Proposal Task 1.3 | Precision config exists |
| NL-driven checker synthesis + evolving loop + orchestration | Proposal Task 3 | Tasks 1+2 foundations usable |
| Incremental graph construction (avoid full rebuild per change) | Proposal Task 2.1 | Harness used interactively on evolving codebases |
| Concurrent query handling in daemon | engineering | Profiling shows serial handling is a bottleneck |
| Property-graph storage backend for subgraph retrieval at scale | Proposal Task 2.1 | Linux-kernel-scale targets |
