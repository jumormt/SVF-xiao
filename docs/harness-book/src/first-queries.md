# First Queries

Question: did the harness load the program I intended, and what query surface
is available?

Fixture: `svf-llvm/tools/Harness/tests/fixtures/demo.c`.

## 1. Compile The Fixture

```bash
source ./setup.sh
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/demo.ll svf-llvm/tools/Harness/tests/fixtures/demo.c
```

The source contains a simple heap lifecycle:

```c
char* b = make_buf(8);
fill(b);
free(b);
return b[0];
```

That final load is deliberately after `free`. Later chapters will use it for
points-to and value-flow questions.

## 2. Ask For A Summary

```bash
Release-build/bin/svf-harness --oneshot summary /tmp/demo.ll
```

Representative output:

```json
{
  "functions": 9,
  "icfg_nodes": 93,
  "pag_nodes": 183,
  "svfg_nodes": 223
}
```

Interpretation:

- `functions` counts definitions plus external declarations such as `malloc`
  and `free`.
- `icfg_nodes` is the interprocedural control-flow graph size.
- `pag_nodes` is the program assignment graph size used by pointer analysis.
- `svfg_nodes` is the sparse value-flow graph size.

Use `summary` as a sanity check. Zero or tiny graph counts usually mean the
wrong input was loaded or compilation failed.

## 3. List Functions Before Naming Them

```bash
Release-build/bin/svf-harness --oneshot functions \
  --params '{"pattern":"free|malloc|use_after_free"}' /tmp/demo.ll
```

Representative output:

```json
{
  "functions": [
    {"name": "free", "is_decl": true, "loc": {"file": "", "line": 0}},
    {"name": "malloc", "is_decl": true, "loc": {"file": "", "line": 0}},
    {"name": "use_after_free", "is_decl": false,
     "loc": {"file": ".../demo.c", "line": 7}}
  ],
  "total": 3,
  "truncated": false
}
```

Interpretation:

- Empty `loc.file` and line `0` mean there is no source body in this module.
  That is normal for declarations.
- `pattern` is an ECMAScript regex matched against function names.
- `truncated: false` means the list is complete. If it is true, narrow the
  pattern before drawing conclusions.

## 4. Read The Schema

```bash
Release-build/bin/svf-harness --oneshot schema /tmp/demo.ll > /tmp/schema.json
python3 - <<'PY'
import json
j = json.load(open("/tmp/schema.json"))
print(len(j["methods"]), "methods")
print(", ".join(m["name"] for m in j["methods"][:8]))
PY
```

The schema describes:

- method names and parameters;
- graph node and edge kinds;
- evidence fields;
- active program metadata.

Treat `schema` as the contract for your checkout. Treat this book as a guide
for using that contract.

## 5. Check The Active Analysis Configuration

```bash
Release-build/bin/svf-harness --oneshot analysis_config /tmp/demo.ll
```

Use this before comparing precision-sensitive results. It reports the active
pointer-analysis defaults, SVFG mode, and supported secondary surfaces such as
CFLAlias, FlowDDA, SABER, MTA, and AE.

## Check Yourself

- Can you explain why `free` has no source location?
- Can you name one reason `functions` might return `truncated: true`?
- Can you find the current parameter contract for `vfpath` in `/tmp/schema.json`?

Next: use these same names and locations to explore program structure.
