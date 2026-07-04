---
name: svf-program-analysis
description: Natural-language program analysis with the SVF harness CLI/MCP. Use when Codex needs to inspect LLVM bitcode, C/C++ fixtures, SVF/Test-Suite cases, or answer questions about functions, callers/callees, CFG, def-use, points-to, aliasing, SVFG value-flow paths, reachability, CFLAlias, FlowDDA, SABER leak/double-free/file-leak summaries, MTA thread/MHP behavior, AE abstract states, or harness schema/config.
---

# SVF Program Analysis

## Overview

Use the in-tree `svf-harness` as the analysis engine and translate the user's
natural-language question into a small number of structured harness queries.
Prefer evidence-backed answers: cite source locations, node kinds, graph ids,
paths, state entries, and the exact query methods used.

## First Steps

1. Locate the SVF repo. Prefer the current workspace if it contains
   `Release-build/bin/svf-harness`; otherwise search likely paths or ask for
   the repo/bitcode path.
2. Ensure the harness binary exists. If missing, build it from the repo:
   `cmake --build Release-build --target svf-harness -j2`.
3. If the user provides C/C++ source, compile it to LLVM IR with debug info and
   value names before querying:
   `clang -S -emit-llvm -g -O0 -fno-discard-value-names -o /tmp/input.ll input.c`.
4. Call `schema` or `analysis_config` first when the method surface or active
   precision matters.
5. Use the smallest query that answers the question. Avoid dumping full graphs
   unless the user asks for broad inventory.

## Tooling

Use `scripts/svf_harness_query.py` for stable CLI calls:

```bash
python3 ~/.codex/skills/svf-program-analysis/scripts/svf_harness_query.py \
  schema --repo "$SVF_REPO" --bitcode /tmp/input.ll
python3 ~/.codex/skills/svf-program-analysis/scripts/svf_harness_query.py \
  oneshot --repo "$SVF_REPO" --bitcode /tmp/input.ll \
  --method vfpath --params '{"source":{"func":"malloc","ret":true},"sink":{"file":"demo.c","line":11}}'
```

The script is a convenience wrapper around `svf-harness --oneshot`; do not
reimplement SVF logic in Python.

## Query Selection

Load `references/query-recipes.md` when selecting methods for a nontrivial
natural-language request. Core routing:

- Program overview: `summary`, `functions`, `graphs`, `analysis_config`.
- Call relationships: `functions` then `callers`/`callees`.
- Statement order/control flow: `cfg`.
- Variable uses and definitions: `defuse`.
- Pointer targets and aliases: `pts`/`aliases`; compare precision with
  `cfl_pts`/`cfl_aliases` or `dda_pts`/`dda_aliases`.
- Value-flow witness or taint-style question: `vfpath` or `reachable`.
- Whole graph browsing: `graph_nodes`, `graph_edges`, `node`, `neighbors`.
- Resource bugs: `saber_leaks`, `saber_double_frees`, `saber_file_leaks`.
- Thread/MHP question: `mta_summary`, then `mta_mhp`.
- Abstract execution values: `ae_summary`, then `ae_state`.

## Answer Style

Return a concise analysis report:

- State the query methods used.
- Summarize the finding in plain language.
- Include evidence records that matter: function names, source file/line,
  node kind/id, edge/path summary, or abstract-state values.
- Explain MAY-analysis semantics when relevant: SVF answers are conservative
  and usually indicate possible flows/aliases/bugs, not concrete executions.
- If a source location cannot resolve, report nearby lines or suggested anchor
  forms from the harness error.

## Safety And Scale

- Prefer `--oneshot` for one or two queries on small programs.
- For repeated queries on the same bitcode, use the MCP server or a harness
  daemon when available; otherwise repeat `--oneshot`.
- Keep broad graph calls paginated with `limit`/`offset`.
- Use `vfpath` with small `k` and explicit `max_visited` for large programs.
- Treat Test-Suite generated `.pre*.bc`/`.svf.bc` files as disposable and
  clean them after tests if they would pollute CMake globs.
