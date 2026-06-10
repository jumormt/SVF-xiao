# Summary: svf-harness Thin Slice (v0)

**Plan:** `docs/plans/2026-06-10-01-svf-harness-thin-slice.md` (done 2026-06-10)
**Design:** `docs/designs/2026-06-10-svf-harness-thin-slice.md`
**Tags:** harness, llm-interface, svf-api, daemon, mcp, evidence

## What was built

One day, ~30 commits on `harness-v0`: a complete LLM-friendly query harness over SVF.
C++ daemon/CLI (`svf-llvm/tools/Harness/`, 11 JSON-RPC methods over a Unix socket,
self-describing schema with 67 node kinds, uniform evidence records, BFS value-flow
witness paths) + Python MCP wrapper (`mcp/svf_harness_mcp/`) + 33 integration tests +
ctest hook + demo. Full regression 2267/2267. All code confined to Harness/, mcp/, and
2 lines of tools/CMakeLists.txt — upstream merges stay cheap.

## Key decisions (and why)

- **Daemon + thin client over stateless CLI**: SVFIR/Andersen/SVFG build cost is paid
  once at `serve`; clients get ms-latency queries. `--oneshot` retained for CI.
- **Static MCP tools + `schema` as single source of truth**: MCP clients list tools at
  connect time (before any program is loaded), so the plan's "dynamic registration from
  schema()" was unimplementable; static wrappers with a generic `params` dict forward
  verbatim, immune to C++ param evolution.
- **Evidence `kind` from toString() prefixes, not GNodeK enums**: SVF prints alias names
  that exist in no enum (FormalINPHISVFGNode/ActualOUTPHISVFGNode, FormalParmPHI/
  ActualRetPHI). schema() enumerates from the same source; `check_schema_kinds.py`
  enforces the invariant textually against svf/lib sources.
- **k-paths = one shortest path per distinct sink node** (single BFS) — honest about
  incompleteness in the schema text rather than pretending to enumerate alternatives.

## Failed approaches / traps (the valuable part)

1. **SVFGBuilder owns the SVFG via unique_ptr.** A ctor-local builder destroyed the
   SVFG it returned — a use-after-free that *looked fine* for 3 tasks (freed memory
   still readable) until schema()'s allocations clobbered it. Caught only because the
   controller chased a 76 vs 22055 node-count discrepancy across task reports; the spec
   reviewer pinned it with a gdb hardware watchpoint. Lesson: SVF builder objects must
   outlive their products (upstream's own SrcSnkDDA holds the builder as a member);
   cross-report numeric inconsistencies are bugs until proven otherwise.
2. **VFGNode::getSourceLoc() is never populated by SVF** — evidence locs must come from
   the wired ICFGNode. All value-flow evidence would silently have empty locations.
3. **add_llvm_executable inherits -fno-exceptions**; the harness needs per-target
   `-fexceptions` (safe: throws never cross SVF/LLVM frames).
4. **SVF stat output pollutes stdout** — `-stat=false` must be injected for pure-JSON.
5. **clang lowers function-pointer array initializers to llvm.memcpy from a const
   global, and SVF's MEMCPY summary does not propagate function pointers through it** —
   indirect-call fixtures need explicit element stores. (Possible upstream improvement.)
6. **getSourceLoc() string formats are inconsistent** (instructions use `"fl"`,
   functions use `"file"`; some prefix junk outside the braces) — tolerant key-scanning
   parser, not JSON parsing.
7. **MCP SDK runs sync tools on the event loop** — all tools must be async with
   blocking socket I/O in `anyio.to_thread`, else a 600s program load freezes the
   whole MCP session (keepalives included).

## Reusable patterns

- **Subagent-driven dev with two-stage review caught 2 critical bugs** (UAF, daemon
  wedge) that single-pass implementation would have shipped. The "controller chases
  cross-report inconsistencies" habit paid for itself.
- Truncation contract (`total` + `truncated` + named cap constants on every list);
  error messages that embed recovery actions (did-you-mean, nearest lines, anchor
  forms) — designed for LLM self-correction, validated in MCP smoke tests.
- Single dispatch table backing dispatch + methodNames + schema implemented-flags:
  structural impossibility of drift beats discipline.
- Test-Suite regression MUST be serial (`ctest` without `-j`) — parallel runs corrupt
  shared generated `.pre.svf.bc` files (~106 spurious failures).

## Follow-ups (see docs/FUTURE.md)

Daemon hardening items (signal race, socket perms, oversize-reply drain); `-32000`
errors carry hints in `message` not `data.hint` (design said structured — deliberate
deferral); aliases v0 same-function scope; L_Q query language is the natural next epic.
