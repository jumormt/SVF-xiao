# Harness Tutorial Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the `svf-harness` mdBook from a short method survey into a tutorial-quality book with a task-driven learning path and full cookbook reference.

**Architecture:** Keep `docs/harness-book/` as the primary book source. Expand the existing chapters into tutorial chapters, add a dedicated `cookbook.md`, and strengthen `check_coverage.py` so method coverage requires a real cookbook entry instead of a stray method-name mention.

**Tech Stack:** mdBook Markdown, standard-library Python coverage checker, existing `svf-harness` schema output, existing tutorial example shell scripts.

---

## Task 1: Design And LDD State

**Files:**
- Create: `docs/designs/2026-07-04-harness-tutorial-redesign.md`
- Create: `docs/plans/2026-07-04-11-harness-tutorial-redesign.md`
- Modify: `docs/PROGRESS.md`

- [x] **Step 1: Save the approved tutorial redesign**

Capture the two-layer structure: task-driven chapters plus complete cookbook.

- [x] **Step 2: Update LDD progress**

Add the new plan to the Plans Index, make it the immediate Next Step, and log
the session start.

## Task 2: Strengthen Coverage Checks

**Files:**
- Modify: `docs/harness-book/check_coverage.py`

- [x] **Step 1: Add cookbook-entry validation**

Require `docs/harness-book/src/cookbook.md` to contain one second-level heading
per live method in the form `## \`method_name\``.

- [x] **Step 2: Add standard-section validation**

For every method entry, require these third-level sections:

- `Purpose`
- `Parameters`
- `Result`
- `Example`
- `Interpretation`
- `Follow-ups`

- [x] **Step 3: Verify the checker fails before cookbook work**

Run the checker against the current book and confirm it reports missing
`cookbook.md` or missing entries.

## Task 3: Rebuild The Book Shape

**Files:**
- Modify: `docs/harness-book/src/SUMMARY.md`
- Modify: `docs/harness-book/src/intro.md`
- Modify: `docs/harness-book/src/setup.md`
- Modify: `docs/harness-book/src/first-queries.md`
- Create: `docs/harness-book/src/cookbook.md`

- [x] **Step 1: Update table of contents**

Split the book into:

- `Start Here`
- `Tutorials`
- `Cookbook`
- `Agent Workflows`
- `Troubleshooting`

- [x] **Step 2: Make the introduction set expectations**

Explain that readers should learn by running the workflows, then use the
cookbook for method-level recall.

- [x] **Step 3: Make setup actionable**

Include build commands, environment pitfalls, compile flags, daemon vs
`--oneshot`, scratch-directory guidance, and how to run companion scripts.

- [x] **Step 4: Make first queries tutorial-grade**

Use `demo.c` and real commands to teach `summary`, `functions`, `schema`, and
`analysis_config` with output interpretation.

## Task 4: Expand Task-Driven Tutorial Chapters

**Files:**
- Modify: `docs/harness-book/src/program-structure.md`
- Modify: `docs/harness-book/src/calls-and-cfg.md`
- Modify: `docs/harness-book/src/pointers.md`
- Modify: `docs/harness-book/src/value-flow.md`
- Modify: `docs/harness-book/src/graph-browsing.md`
- Modify: `docs/harness-book/src/bug-checkers.md`
- Modify: `docs/harness-book/src/precision-surfaces.md`
- Modify: `docs/harness-book/src/threads-and-ae.md`

- [x] **Step 1: Add workflow framing to each chapter**

Each chapter should start with the concrete analysis question it answers and
the fixture or bitcode it uses.

- [x] **Step 2: Add commands and representative output snippets**

Use compact JSON snippets that show the fields a user must interpret.

- [x] **Step 3: Add interpretation and common pitfalls**

Teach empty locations, truncation, MAY analysis, unresolvable anchors, expensive
queries, and graph-node evidence where relevant.

- [x] **Step 4: Add checks and next-query guidance**

End each chapter with a short "check yourself" section and the next natural
query.

## Task 5: Add Complete Cookbook

**Files:**
- Create/Modify: `docs/harness-book/src/cookbook.md`

- [x] **Step 1: Add all 28 method entries**

Every live schema method must have a standard entry with purpose, parameters,
result, example, interpretation, and follow-ups.

- [x] **Step 2: Keep examples concise**

Use one-line or short multi-line commands; link back to tutorial chapters for
longer explanations.

## Task 6: Agent Workflow And Troubleshooting Polish

**Files:**
- Modify: `docs/harness-book/src/mcp-and-codex.md`
- Modify: `docs/harness-book/src/skills.md`
- Modify: `docs/harness-book/src/troubleshooting.md`
- Modify: `docs/harness-book/README.md`

- [x] **Step 1: Teach the MCP lifecycle**

Document `load_program`, `schema`, query tools with nested `params`, and
`unload_program`.

- [x] **Step 2: Teach skill responsibilities**

Clarify when to use `$svf-program-analysis` and `$svf-harness-maintainer`.

- [x] **Step 3: Expand troubleshooting**

Cover no debug info, stale env vars, slow CFLAlias, daemon socket failure,
generated Test-Suite artifacts, and schema drift.

## Task 7: Verification And LDD Closeout

**Files:**
- Modify: `docs/PROGRESS.md`
- Modify: `docs/plans/2026-07-04-11-harness-tutorial-redesign.md`

- [x] **Step 1: Generate live schema**

Run:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/svf-mdbook-demo.ll svf-llvm/tools/Harness/tests/fixtures/demo.c
Release-build/bin/svf-harness --oneshot schema /tmp/svf-mdbook-demo.ll \
  > /tmp/svf-harness-schema.json
```

- [x] **Step 2: Run coverage checker**

Run:

```bash
python3 docs/harness-book/check_coverage.py \
  --schema /tmp/svf-harness-schema.json --book docs/harness-book
```

Expected: PASS with all live methods covered by cookbook entries.

- [x] **Step 3: Build mdBook**

Run:

```bash
mdbook build docs/harness-book
```

Expected: build succeeds and writes only ignored output under
`docs/harness-book/book/`.

- [x] **Step 4: Run companion examples**

Run:

```bash
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
  bash svf-llvm/tools/Harness/examples/run_all.sh
```

Expected: all available tutorial scripts pass or documented optional skips occur.

- [x] **Step 5: Update LDD**

Mark this plan done or in-progress with exact verification results, update
`docs/PROGRESS.md` Next Steps, and append a session log entry.

## Results

- Rebuilt `docs/harness-book/` from a 738-line method survey into a 2479-line
  tutorial book with task-driven chapters and a full query cookbook.
- Added `src/cookbook.md` with all 28 live schema methods and a standard
  purpose/parameters/result/example/interpretation/follow-ups template.
- Strengthened `check_coverage.py` so method coverage requires real cookbook
  entries and standard sections, not just stray method-name mentions.
- Updated Codex MCP docs to match the current `bash -lc` project config.
- Verified the checker failed before cookbook work with missing `cookbook.md`.
- Verified final coverage with 28/28 methods, `mdbook v0.5.3` build, companion
  examples 5/5, and generated Test-Suite artifact count 0 after cleanup.
