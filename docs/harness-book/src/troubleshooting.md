# Troubleshooting

This chapter starts from symptoms. For method detail, use
[Query Cookbook](cookbook.md).

## No Source Locations

Symptom:

```json
{"loc":{"file":"","line":0}}
```

For external declarations and synthetic nodes, this is normal. For your own
functions, the bitcode probably lacks debug info. Recompile with:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names -o /tmp/input.ll input.c
```

When debug info is unavailable, prefer `{func,ret}` and `{func,arg}` anchors
over `{file,line}` anchors.

## Query Returns Too Many Functions

In CLI mode, narrow the regex:

```bash
Release-build/bin/svf-harness --oneshot functions \
  --params '{"pattern":"^free$"}' /tmp/demo.ll
```

In MCP mode, check that you used nested `params`:

```json
{"params":{"pattern":"free"}}
```

Flat keys are ignored by the MCP wrapper:

```json
{"pattern":"free"}
```

## Daemon Unreachable

For CLI mode, check that the socket exists and the daemon is still alive:

```bash
ls -l /tmp/h.sock
```

If startup failed, rerun `serve` with stdout/stderr redirected to a log:

```bash
Release-build/bin/svf-harness serve /tmp/demo.ll --socket /tmp/h.sock \
  >/tmp/svf-harness.log 2>&1
```

For MCP mode, call `load_program` again and inspect the returned daemon/log
details if startup fails.

## Build Toolchain Mismatch

This Ubuntu 20.04 machine uses a conda LLVM 21 symlink. Build with stale env
vars removed:

```bash
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && cmake --build Release-build --target svf-harness -j2'
```

If `clang` or CMake finds the wrong LLVM, check your shell environment first.

## Generated Test-Suite Artifacts

SVF preprocessing writes generated bitcode next to inputs. Before reconfiguring
CMake or running broad Test-Suite checks, inspect:

```bash
find Test-Suite/test_cases_bc -name '*.pre*.bc' -o -name '*.svf.bc' | wc -l
```

Clean stale generated artifacts when needed:

```bash
git -C Test-Suite clean -fdx
```

Do not run full Test-Suite with `ctest -j`; parallel runs can corrupt shared
generated `.pre.svf.bc` files.

## Slow CFLAlias

`cfl_pts` and `cfl_aliases` can be much slower than default `pts` and
`aliases` on larger programs. Use focused anchors and explicit time budgets.
Start with Andersen, then ask CFLAlias only for the relation that matters.

## AE Or MTA Returns Little

Run the summary query first:

```bash
Release-build/bin/svf-harness --oneshot mta_summary /tmp/thread_mhp.ll
Release-build/bin/svf-harness --oneshot ae_summary /tmp/ae_state.ll
```

If the summary shows no useful coverage, a specific `mta_mhp` or `ae_state`
query will not become useful by changing only the line number.

## Schema Drift

Symptom: docs mention a method but the binary rejects it, or MCP lacks a CLI
method.

Run:

```bash
Release-build/bin/svf-harness --help
Release-build/bin/svf-harness --oneshot schema /tmp/demo.ll > /tmp/schema.json
python3 docs/harness-book/check_coverage.py \
  --schema /tmp/schema.json --book docs/harness-book
```

For implementation changes, keep CLI help, `Schema.cpp`, MCP tools, tests,
examples, and docs synchronized.

## Claims Need Evidence

Do not report a bug from a method name alone. Capture:

- query JSON;
- source lines;
- node kinds and ids;
- callsite evidence;
- edge kinds for paths;
- `total` and `truncated`;
- MAY-analysis caveat.
