# SVF Harness mdBook

This directory contains the primary `svf-harness` tutorial book. It has two
layers:

- task-driven tutorials under `src/`;
- a complete method reference in `src/cookbook.md`.

Build:

```bash
mdbook build docs/harness-book
```

On Ubuntu 20.04 focal, recent GNU mdBook binaries may require a newer glibc.
Use the `x86_64-unknown-linux-musl` release asset if needed.

Coverage check:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/svf-mdbook-demo.ll svf-llvm/tools/Harness/tests/fixtures/demo.c
Release-build/bin/svf-harness --oneshot schema /tmp/svf-mdbook-demo.ll \
  > /tmp/svf-harness-schema.json
python3 docs/harness-book/check_coverage.py \
  --schema /tmp/svf-harness-schema.json --book docs/harness-book
```

The check verifies:

- every live `schema.methods[].name` appears in the book;
- every live method has a `## \`method\`` entry in `src/cookbook.md`;
- every cookbook entry has the standard sections;
- `src/SUMMARY.md` links point at existing files.
