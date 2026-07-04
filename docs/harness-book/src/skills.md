# Codex Skills

Question: when should Codex use a skill, and how is that different from MCP?

MCP tools execute live actions. Skills teach Codex how to choose and interpret
those actions.

## 1. Install Repo-Shipped Skills

```bash
bash codex/install-codex-assets.sh
```

The installer copies:

- `codex/skills/svf-program-analysis`;
- `codex/skills/svf-harness-maintainer`.

into `${CODEX_HOME:-$HOME/.codex}/skills/`. Restart Codex after installing or
updating skills.

## 2. Use `$svf-program-analysis` For Questions About Programs

Use it when the task is natural-language analysis over C/C++, LLVM IR, bitcode,
or Test-Suite fixtures:

```text
$svf-program-analysis In /tmp/demo.ll, show whether malloc's return can flow to demo.c:11.
```

The skill should guide Codex to:

- compile source to LLVM IR when needed;
- load bitcode through the harness or MCP;
- call `schema` first;
- pick focused queries such as `functions`, `pts`, `vfpath`, `mta_mhp`, or
  `ae_state`;
- report evidence and MAY-analysis limits.

## 3. Use `$svf-harness-maintainer` For Harness Changes

Use it when changing the harness implementation, query surfaces, MCP wrapper,
tests, examples, or docs:

```text
$svf-harness-maintainer add a new query method and keep CLI, schema, MCP, tests, and docs synchronized.
```

The maintainer skill exists because one harness method touches many surfaces:

- C++ method table and query implementation;
- `Schema.cpp`;
- CLI help;
- MCP server method list;
- MCP smoke tests;
- README, tutorial, and example docs;
- LDD progress files.

## 4. Skill And MCP Division

Use MCP for live data:

```text
Load this bitcode and run vfpath.
```

Use skills for workflow discipline:

```text
Choose the right harness query sequence and explain the evidence.
```

In practice, a good Codex run uses both: the skill chooses the query plan, MCP
executes the plan, and the final answer cites the evidence.

## 5. Explicit Is Better For This Project

Codex can select skills implicitly when a request matches their descriptions.
Still, explicit `$skill-name` prompts are useful here because program analysis
and harness maintenance have different safety requirements.

Use:

- `$svf-program-analysis` for analysis questions;
- `$svf-harness-maintainer` for code, schema, MCP, examples, or docs changes.

The repository `AGENTS.md` records durable project rules. It is not a skill,
but Codex reads it before work starts.
