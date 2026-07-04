# Plan: svf-harness Analysis Configuration Thin Slice

**Epic:** E3 / composable precision configuration
**Design:** Incremental follow-up to `docs/plans/2026-07-03-01-harness-graph-queries.md`; this is the first configuration slice, not full DDA/CFL/SABER/MTA/AE integration.

## Summary

Expose the harness analysis configuration through CLI and MCP, and make SVFG construction mode actually configurable. This answers the immediate gap that SVF has multiple graph/precision construction modes while keeping the daemon stable: only the already-used Andersen pipeline is active, and heavier subanalyses are reported as planned surfaces for later dedicated slices.

**Decisions locked in:**
- The default remains current behavior: AndersenWaveDiff plus full SVFG.
- First configurable knobs are SVFG construction only: `mode: full|ptr-only`, `indirect_calls: bool`, and `post_opts: bool`.
- CLI gets `--analysis-config JSON` for `serve` and `--oneshot`; MCP `load_program` gets optional `analysis_config`.
- Add one daemon method, `analysis_config`, so clients can confirm the active configuration and supported/planned surfaces.
- DDA/CFL/SABER/MTA/AE are not run in this plan; they are advertised as future surfaces with clear status to avoid fake coverage.

---

## Phase 1: Tests First

Add failing tests for configuration discovery and actual SVFG mode switching.

### [x] Task 1.1: Add daemon schema/config tests
- [x] In `svf-llvm/tools/Harness/tests/run_tests.py`, add `test_schema_analysis_config_method` expecting `analysis_config` after `neighbors` with non-empty docs.
- [x] Add `test_analysis_config_default` expecting `analysis_config.svfg.mode == "full"` and `pointer_analysis.active == "andersen-wave-diff"`.
- [x] Run those tests before implementation and confirm they fail because the method does not exist.

### [x] Task 1.2: Add configurable SVFG behavior test
- [x] In `svf-llvm/tools/Harness/tests/run_tests.py`, add a helper to pass `--analysis-config`.
- [x] Add `test_svfg_ptr_only_config_changes_graph_kind` that runs `schema` or `graphs` with `{"svfg":{"mode":"ptr-only"}}` and verifies `analysis_config.svfg.mode == "ptr-only"` plus graph counts remain non-empty.
- [x] Add `test_invalid_analysis_config_rejected` for an unknown SVFG mode.

### [x] Task 1.3: Add MCP drift test
- [x] In `mcp/svf_harness_mcp/test_smoke.py`, add `analysis_config` to the static query tool list.
- [x] Add a smoke assertion that `load_program(..., analysis_config={"svfg":{"mode":"ptr-only"}})` returns `analysis_config.svfg.mode == "ptr-only"` in the load summary.

## Phase 2: Core Configuration Plumbing

Add a small typed config object and thread it through construction.

### [x] Task 2.1: Add `HarnessConfig`
- [x] In `svf-llvm/tools/Harness/QueryEngine.h`, add a small struct with defaults:
  - `pointerAnalysis = "andersen-wave-diff"`
  - `svfgMode = "full"`
  - `svfgIndirectCalls = Options::SVFGWithIndirectCall()`
  - `svfgPostOpts = Options::OPTSVFG()`
- [x] Add `static HarnessConfig fromJson(const nlohmann::json&)` or equivalent helper in `QueryEngine.cpp`.
- [x] Reject unsupported pointer-analysis names and SVFG modes with clear `runtime_error` messages.

### [x] Task 2.2: Build SVFG from config
- [x] Change `QueryEngine` construction to accept `HarnessConfig`.
- [x] Construct `SVFGBuilder` with `svfgIndirectCalls` and `svfgPostOpts`.
- [x] Call `buildFullSVFG` for `mode=full` and `buildPTROnlySVFG` for `mode=ptr-only`.
- [x] Preserve the current default behavior when no config is provided.

### [x] Task 2.3: Add `analysis_config` method
- [x] Register `analysis_config` in `QueryEngine::methodTable()` after `neighbors`.
- [x] Return active config, supported SVFG modes, supported active pointer analyses, and planned surfaces for DDA/CFL/SABER/MTA/AE.
- [x] Include the same config under `schema.program.analysis_config`.

## Phase 3: CLI And MCP

Expose the config through both user entry points.

### [x] Task 3.1: CLI `--analysis-config`
- [x] In `svf-harness.cpp`, parse `--analysis-config JSON` in `serve` and `--oneshot`.
- [x] Strip it before passing args to `OptionBase`.
- [x] Update `--help` with the new flag and include `analysis_config` in the methods line.

### [x] Task 3.2: MCP `load_program` config
- [x] In `mcp/svf_harness_mcp/server.py`, add optional `analysis_config: dict[str, Any] | None = None` to `load_program`.
- [x] When provided, spawn `svf-harness serve --analysis-config <json> ...`.
- [x] After the daemon is ready, call both `summary` and `analysis_config` and include the latter in the `load_program` result.
- [x] Add static wrapper docs for `analysis_config`.

### [x] Task 3.3: Docs
- [x] Update `svf-llvm/tools/Harness/README.md` with the new method and config example.
- [x] Update `mcp/svf_harness_mcp/README.md` and tutorial 06 counts from 18 to 19 tools and 16 to 17 daemon methods.
- [x] Update `docs/FUTURE.md` to mark SVFG construction config as started/done while keeping DDA/CFL/SABER/MTA/AE deferred.

## Verification

- [x] Red tests fail before implementation.
- [x] `svf-harness --oneshot analysis_config demo.ll` returns default config.
- [x] `svf-harness --oneshot analysis_config --analysis-config '{"svfg":{"mode":"ptr-only"}}' demo.ll` returns ptr-only.
- [x] Full harness suite passes.
- [x] Standalone MCP smoke passes.
- [x] `ctest -R "harness_"` passes.
- [x] `docs/PROGRESS.md` updated with results and next steps.

## Results

Completed 2026-07-03.

- Added `analysis_config` as daemon method 17 and MCP tool 19.
- Added `--analysis-config JSON` to `serve` and `--oneshot`.
- Added MCP `load_program(bitcode_paths, analysis_config=None)` forwarding and active-config readback.
- Implemented actual SVFG construction switching: default `full`; configurable `ptr-only`, `indirect_calls`, `post_opts`.
- Manual demo on `demo.c`: full default reported `mode=full`; ptr-only daemon reported `mode=ptr-only`, `indirect_calls=true`, and `graphs.svfg` changed from the prior full graph shape to 87 nodes / 32 edges.
- Verification: full harness 43/43, standalone MCP smoke 4/4, examples 5/5, CTest harness 2/2.
