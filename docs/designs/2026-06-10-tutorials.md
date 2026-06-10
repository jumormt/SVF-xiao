# Design: svf-harness Tutorials

**Date:** 2026-06-10  **Status:** Approved (user)

Six English markdown walkthroughs in `docs/tutorials/` (01 getting-started, 02 exploring
a program, 03 pointer/dataflow + anchors, 04 value-flow witnesses, 05 real-world program
on crux-bc, 06 Claude Code via MCP) plus matching executable scripts in
`svf-llvm/tools/Harness/examples/` (01–05 + run_all.sh; 06 ships a .mcp.json sample
instead — MCP sessions aren't scriptable).

Decisions:
1. All command outputs in the docs come from real runs; scripts assert the key output
   features and run in CI (new `harness_examples` ctest, Test-Suite-gated like the rest).
2. 01–04 use tests/fixtures programs (second-scale); 05 uses Test-Suite crux-bc
   (script skips cleanly when Test-Suite is absent).
3. Uniform per-tutorial structure: Goal → Prereqs → Steps (command + output +
   interpretation) → What you learned → Next. Interpretation explains the WHY
   (empty decl locs, may-analysis semantics, anchor forms).
4. 06 covers `claude mcp add`, load_program flow, three effective question patterns,
   and one sample session transcript.

Non-goals: videos/screenshots, Chinese translation, notebooks, SVF-internals theory
(link upstream wiki).
