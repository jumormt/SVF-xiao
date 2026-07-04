# Introduction

`svf-harness` is a query layer over SVF for repeatable program-analysis
sessions. It loads LLVM IR once, builds SVF analysis state, and answers JSON
queries about functions, calls, control flow, points-to sets, aliases, value
flow, graph neighborhoods, bug checkers, threads, and abstract execution.

This book has two jobs.

First, it teaches workflows. The tutorial chapters start with a concrete
question, show the fixture or bitcode being analyzed, run exact commands, and
explain the result fields that matter. Follow these chapters in order if you
are learning the harness.

Second, it provides a method cookbook. Once you know the workflow, use
[Query Cookbook](cookbook.md) to look up parameters, result shapes, examples,
and follow-up queries for every live method in `schema`.

The schema remains the contract. This book explains how to use the contract,
but the daemon's `schema` method is the authoritative source for method names,
parameters, node kinds, edge kinds, and evidence fields in the checkout you are
running.

## What You Will Learn

- How to compile source with the debug information the harness needs.
- When to use daemon mode versus `--oneshot`.
- How to read `loc`, `evidence`, `total`, and `truncated` fields.
- How to turn broad questions into stable query sequences.
- How to avoid common mistakes such as treating MAY analysis as proof of a
  concrete runtime path.
- How Codex uses the same harness through MCP and repository skills.

## Fixtures Used By The Book

Most tutorials use small fixtures under
`svf-llvm/tools/Harness/tests/fixtures/`:

- `demo.c`: allocation, fill, free, and a deliberate use-after-free load.
- `indirect.c`: function-pointer dispatch for indirect-call examples.
- `chain.c`: long value-flow path used to show path truncation.
- `thread_mhp.c`: thread creation and may-happen-in-parallel checks.
- `ae_state.c`: abstract-execution state lookup by source line.

The real-program chapter material uses Test-Suite bitcode when available,
especially `Test-Suite/test_cases_bc/crux-bc/bc.bc`.

## How To Read This Book

Run the commands. The output snippets are representative and intentionally
small, but the commands are the point. If a result surprises you, run `schema`,
then look up the method in the cookbook before changing the query.
