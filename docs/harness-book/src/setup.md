# Setup

This chapter gets you to a repeatable harness session. All commands assume the
repository root as the working directory.

## Build The Harness

This checkout's shell environment may point at stale LLVM or SVF installs.
Build with those variables removed:

```bash
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash ./build.sh
```

For an incremental rebuild of the harness only:

```bash
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && cmake --build Release-build --target svf-harness -j2'
```

The binary should be:

```bash
Release-build/bin/svf-harness
```

## Put The Toolchain On PATH

Before compiling tutorial fixtures:

```bash
source ./setup.sh
```

If you run commands inside a fresh shell, source it again. The tutorials use
`clang` from the configured LLVM toolchain.

## Compile Source For Good Evidence

The harness analyzes LLVM IR or bitcode, not C source. Use debug info and keep
value names:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
  -o /tmp/demo.ll svf-llvm/tools/Harness/tests/fixtures/demo.c
```

The flags matter:

- `-g` gives source files and line numbers for `loc` and `evidence`.
- `-O0` keeps source and IR close enough for tutorials.
- `-fno-discard-value-names` keeps names such as `%b`, which makes name-based
  anchors easier to use.

SVF may write generated `.pre.svf.bc` files next to the input. Compile tutorial
inputs into `/tmp` or another writable scratch directory.

## Choose Daemon Or Oneshot

Use daemon mode when you will ask several questions about the same program:

```bash
Release-build/bin/svf-harness serve /tmp/demo.ll --socket /tmp/h.sock &
Release-build/bin/svf-harness summary --socket /tmp/h.sock
Release-build/bin/svf-harness shutdown --socket /tmp/h.sock
```

Use `--oneshot` for one query while writing examples or smoke tests:

```bash
Release-build/bin/svf-harness --oneshot summary /tmp/demo.ll
```

Daemon mode pays the analysis cost once. `--oneshot` rebuilds analysis state
for every command.

## Run Companion Scripts

The older tutorial scripts remain useful because they assert expected behavior:

```bash
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
  bash svf-llvm/tools/Harness/examples/run_all.sh
```

Use the scripts to check your environment, then use this book to understand
what the queries mean.

## Build This Book

If `mdbook` is available:

```bash
mdbook build docs/harness-book
```

On Ubuntu 20.04, recent GNU mdBook binaries may require a newer glibc. Use a
musl mdBook binary if the prebuilt GNU binary does not start.
