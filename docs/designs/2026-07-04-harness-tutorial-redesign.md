# SVF Harness Tutorial Redesign

## Problem

The current mdBook covers every live `svf-harness` method, but it reads like a
short API summary. Most chapters are only a few dozen lines and do not teach a
user how to move from source code to a defensible analysis answer. The older
`docs/tutorials/` pages are closer to real tutorials because they include
goals, fixtures, commands, output interpretation, and companion scripts.

## Goal

Turn `docs/harness-book/` into the primary tutorial-level documentation for
`svf-harness`: a task-driven learning path backed by a complete method
cookbook.

## Audience

- A new harness user who knows C/C++ and LLVM IR basics but not SVF internals.
- A researcher or agent author who needs reliable query recipes and evidence
  interpretation.
- A future maintainer adding new query methods who needs a documentation shape
  that resists drift.

## Structure

The book should have two layers:

1. Task-driven tutorial chapters that teach workflows end to end.
2. A cookbook reference that covers every query method with a uniform template.

Tutorial chapters should use real fixtures or Test-Suite bitcode and include:

- what question the chapter answers;
- source or IR context;
- exact compile and harness commands;
- representative JSON output snippets;
- field interpretation;
- common wrong interpretations;
- checks the reader can run to confirm they understood the result.

The cookbook should include one entry per method:

- purpose;
- parameters;
- result shape;
- minimal command;
- interpretation notes;
- good follow-up queries.

## Chapter Plan

The tutorial layer should cover:

- setup and first daemon session;
- schema-first discovery;
- program structure and source evidence;
- call graph and CFG navigation;
- pointer, alias, and def-use anchors;
- value-flow witnesses and reachability triage;
- graph browsing for SVF internals;
- bug checker queries;
- precision surface comparisons;
- MTA and abstract execution;
- Codex MCP and skill workflows.

The cookbook layer should cover all current schema methods:

`schema`, `summary`, `functions`, `callers`, `callees`, `cfg`, `defuse`, `pts`,
`aliases`, `cfl_pts`, `cfl_aliases`, `dda_pts`, `dda_aliases`, `saber_leaks`,
`saber_double_frees`, `saber_file_leaks`, `mta_summary`, `mta_mhp`,
`ae_summary`, `ae_state`, `vfpath`, `reachable`, `graphs`, `graph_nodes`,
`graph_edges`, `node`, `neighbors`, and `analysis_config`.

## Verification

The existing coverage checker should be strengthened so that method names are
not enough. It should also require a cookbook anchor for each live method and
require standard sections such as purpose, parameters, result, example,
interpretation, and follow-ups.

Verification should include:

- generating live schema from `Release-build/bin/svf-harness`;
- running `docs/harness-book/check_coverage.py`;
- building the mdBook;
- running tutorial examples with `svf-llvm/tools/Harness/examples/run_all.sh`;
- updating LDD with exact results.

## Non-Goals

- No new query method implementation.
- No behavior changes to the harness daemon, CLI, or MCP server.
- No attempt to replace generated schema as the authoritative API contract.
