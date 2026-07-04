# SVF Harness mdBook Tutorial Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a tested mdBook tutorial for `svf-harness` that covers all current query methods, Codex MCP usage, and Codex skills usage.

**Architecture:** Add `docs/harness-book/` as the mdBook source tree while keeping existing `docs/tutorials/` as historical walkthroughs and script companions. Add a small Python coverage checker that reads the live harness `schema` output and verifies every method appears in the book, plus a SUMMARY link check.

**Tech Stack:** mdBook Markdown, standard-library Python, existing `svf-harness` schema output, existing MCP wrapper docs.

---

### Task 1: Coverage Check Harness

**Files:**
- Create: `docs/harness-book/check_coverage.py`
- Modify: `docs/PROGRESS.md`

- [x] **Step 1: Add coverage checker**

Create a Python script that:
- reads `--schema /path/to/schema.json`
- walks `--book docs/harness-book`
- verifies all `schema.methods[].name` values appear in book Markdown
- verifies all `src/SUMMARY.md` links point at existing files
- prints a concise pass/fail report

- [x] **Step 2: Verify RED**

Run:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/svf-mdbook-demo.ll svf-llvm/tools/Harness/tests/fixtures/demo.c
Release-build/bin/svf-harness --oneshot schema /tmp/svf-mdbook-demo.ll \
  > /tmp/svf-harness-schema.json
python3 docs/harness-book/check_coverage.py \
  --schema /tmp/svf-harness-schema.json --book docs/harness-book
```

Expected: FAIL because the mdBook source files do not exist yet.

### Task 2: mdBook Structure

**Files:**
- Create: `docs/harness-book/book.toml`
- Create: `docs/harness-book/src/SUMMARY.md`
- Create: core chapter files under `docs/harness-book/src/`
- Modify: `.gitignore`

- [x] **Step 1: Add mdBook skeleton**

Create the book config, table of contents, and chapters:
- `intro.md`
- `setup.md`
- `first-queries.md`
- `concepts.md`
- `program-structure.md`
- `calls-and-cfg.md`
- `pointers.md`
- `precision-surfaces.md`
- `value-flow.md`
- `graph-browsing.md`
- `bug-checkers.md`
- `threads-and-ae.md`
- `mcp-and-codex.md`
- `skills.md`
- `troubleshooting.md`
- `api-cheatsheet.md`

- [x] **Step 2: Ignore build output**

Add `docs/harness-book/book/` to `.gitignore`.

### Task 3: Full Tutorial Content

**Files:**
- Modify: `docs/harness-book/src/*.md`

- [x] **Step 1: Cover all 28 methods**

Document example use and interpretation for:
`schema`, `summary`, `functions`, `callers`, `callees`, `cfg`, `defuse`,
`pts`, `aliases`, `cfl_pts`, `cfl_aliases`, `dda_pts`, `dda_aliases`,
`saber_leaks`, `saber_double_frees`, `saber_file_leaks`, `mta_summary`,
`mta_mhp`, `ae_summary`, `ae_state`, `vfpath`, `reachable`, `graphs`,
`graph_nodes`, `graph_edges`, `node`, `neighbors`, `analysis_config`.

- [x] **Step 2: Cover Codex MCP and skills**

Document:
- project `.codex/config.toml`
- `codex mcp list`
- `load_program -> schema -> query -> unload_program`
- `params` nesting for MCP query tools
- explicit `$svf-program-analysis` and `$svf-harness-maintainer`
- when Codex implicitly selects skills
- how skills and MCP divide responsibilities

### Task 4: Link Existing Docs

**Files:**
- Modify: `docs/README.md`
- Modify: `docs/tutorials/README.md`
- Modify: `svf-llvm/tools/Harness/README.md`
- Modify: `mcp/svf_harness_mcp/README.md`

- [x] **Step 1: Add entry links**

Point existing docs at the new book as the comprehensive tutorial, while keeping
`docs/tutorials/` as the script-backed walkthrough set.

### Task 5: Verification

**Files:**
- Modify: `docs/PROGRESS.md`
- Modify: this plan file

- [x] **Step 1: Run coverage checker**

Run:

```bash
python3 docs/harness-book/check_coverage.py \
  --schema /tmp/svf-harness-schema.json --book docs/harness-book
```

Expected: PASS with all 28 methods covered.

- [x] **Step 2: Build mdBook**

Use a temporary mdBook binary if `mdbook` is not installed, then run:

```bash
mdbook build docs/harness-book
```

Expected: build succeeds and writes only ignored output under
`docs/harness-book/book/`.

- [x] **Step 3: Run MCP smoke**

Run:

```bash
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
  /home/xiao/program/py311-mcp/bin/python mcp/svf_harness_mcp/test_smoke.py -v
```

Expected: 4/4 tests pass.

- [x] **Step 4: Update LDD**

Record exact verification evidence in `docs/PROGRESS.md` and mark this plan
done.

## Results

- Created `docs/harness-book/` mdBook source with 16 chapters.
- Added `docs/harness-book/check_coverage.py` to compare book coverage against
  live `svf-harness schema` output.
- Covered all 28 daemon methods and the Codex MCP / skills workflows.
- Linked the book from existing docs and kept `docs/tutorials/` as the
  script-backed walkthrough set.
- Verified coverage, mdBook build, MCP smoke, examples, whitespace, and
  generated Test-Suite artifact cleanup.
