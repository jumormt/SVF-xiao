# svf-harness tutorials

Six hands-on tutorials for `svf-harness`, the LLM-friendly query daemon/CLI
over SVF's analyses. Read them in order — each builds on the previous one's
vocabulary (evidence records, anchors, witness paths).

| # | Tutorial | What you'll learn |
|---|----------|-------------------|
| 01 | [Getting started](01-getting-started.md) | Compile C to LLVM IR, start the daemon, first queries (`summary`, `functions`), shut down — and the pay-once-at-serve cost model. |
| 02 | [Exploring a program](02-exploring-a-program.md) | The self-describing `schema` contract, regex `functions` search, `callers`/`callees` (including function-pointer calls), and `cfg`. |
| 03 | [Pointer and dataflow queries](03-pointer-and-dataflow.md) | The three variable-anchor forms, `pts` (what may X point to), `aliases`, `defuse` — and how error hints guide you to a working anchor. |
| 04 | [Value-flow witnesses](04-value-flow-witnesses.md) | `vfpath` step-by-step witness paths (the use-after-free money shot), batch `reachable`, and long-path elision with `max_steps`. |
| 05 | [A real-world program](05-real-world-program.md) | GNU bc and bash from Test-Suite: working without debug info, performance expectations, and recovering from a wrong first question (the `xmalloc` lesson). |
| 06 | [Claude Code via MCP](06-claude-code-mcp.md) | Registering the [MCP wrapper](../../mcp/svf_harness_mcp/README.md) in Claude Code, the `params`-nesting trap, three question patterns, and a sample evidence-grounded session. |

## Prerequisites

A built `svf-harness` binary and `clang` on PATH — the
[Harness README quick start](../../svf-llvm/tools/Harness/README.md#quick-start)
covers both. Tutorial 05 additionally needs the SVF Test-Suite cloned
(instructions in its prerequisites); tutorial 06 needs a python ≥ 3.10
with the `mcp` SDK and Claude Code.

## Executable companions

Tutorials 01–05 each have a script in
[`svf-llvm/tools/Harness/examples/`](../../svf-llvm/tools/Harness/examples/)
that runs every step and asserts the outputs pasted in the text;
[`run_all.sh`](../../svf-llvm/tools/Harness/examples/run_all.sh) runs all of
them (also wired into ctest as `harness_examples`). Tutorial 06 has no script
(MCP sessions are interactive), but every tool output it shows was captured
from real calls.
