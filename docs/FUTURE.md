# Deferred Features

Items intentionally out of the v0 thin slice, with trigger conditions.

| Feature | Source | Trigger to start |
|---------|--------|------------------|
| Declarative query language L_Q (Datalog-style, validation + repair hints) | Proposal Task 2.2 | Thin slice in daily use; fixed method set becomes limiting |
| Composable precision space C = D×K×F×H, region policies, Galois boundaries | Proposal Task 1 | Need beyond-Andersen precision in harness queries; SVFG construction config thin slice started 2026-07-03 |
| DDA/CFL/SABER/MTA/AE query/config surfaces | User-deferred after graph browsing, 2026-07-03 | Graph browsing is stable and users need those analyses exposed through CLI/MCP |
| Evidence with path conditions + abstract-state traces | Proposal Task 2.3 | Path-sensitive analysis integrated |
| Cost-aware profiling/contracts per region | Proposal Task 1.3 | Precision config exists |
| NL-driven checker synthesis + evolving loop + orchestration | Proposal Task 3 | Tasks 1+2 foundations usable |
| Incremental graph construction (avoid full rebuild per change) | Proposal Task 2.1 | Harness used interactively on evolving codebases |
| Concurrent query handling in daemon | engineering | Profiling shows serial handling is a bottleneck |
| Persistent graph/storage layer: export/load property-graph snapshots, cache analysis artifacts, and optionally back subgraph retrieval with a graph DB or embedded store | User follow-up after in-memory harness explanation, 2026-07-04; Proposal Task 2.1 | Repeated large-program sessions, cross-version queries, or Linux-kernel-scale targets make daemon rebuilds/subgraph scans too expensive |

## Daemon hardening (deferred from Task 3.1 review, 2026-06-10)

| Item | Trigger |
|------|---------|
| Self-pipe/ppoll to close the signal-vs-accept race window | First report of a hung scripted `kill && wait` |
| Destructor unlink guard (stat dev/ino match) against successor-daemon socket deletion | Kill/restart races observed in practice |
| Socket in `$XDG_RUNTIME_DIR` + umask 0177 (multi-user hardening) | Harness used on shared machines |
| Drain/SHUT_RD before oversize -32600 reply so client can actually read it | An MCP/client actually needs the structured oversize error |

## Perf (confirmed in bash.bc shakedown 2026-06-10)

| Item | Trigger |
|------|---------|
| Lazy function→ValVars index for `aliases` (7.1s on a 603-callsite anchor in bash.bc) | aliases used routinely on large programs |
| Better no-debug-info diagnosis ("module lacks -g" instead of "no vars at line N") | LLM sessions on release bitcode |
