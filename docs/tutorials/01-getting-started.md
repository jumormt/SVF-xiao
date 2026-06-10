# Tutorial 01 — Getting started with svf-harness

## Goal

Run your first svf-harness session: compile a 13-line C program to LLVM IR,
start the analysis daemon on it, ask two basic questions (`summary` and
`functions`), and shut it down cleanly. At the end you will know the daemon
lifecycle and how to read the two simplest result shapes.

Run the whole thing as a script:
[`examples/01-getting-started.sh`](../../svf-llvm/tools/Harness/examples/01-getting-started.sh).

## Prerequisites

- A built `svf-harness` binary — follow the
  [Harness README quick start](../../svf-llvm/tools/Harness/README.md#quick-start).
  The binary lands in `Release-build/bin/svf-harness`.
- `clang` on PATH. From the repo root, `source ./setup.sh` puts the bundled
  LLVM toolchain (and the SVF libraries) on your PATH. If your shell exports
  LLVM_DIR/Z3_DIR/SVF_DIR pointing at other installations, unset them for
  these commands: prefix with `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c
  '...'` as described in the README.
- `python3` (only used to pretty-print JSON in these tutorials).

All commands below are run from the repo root after `source ./setup.sh`.

## Steps

### 1. Meet the program

The fixture `svf-llvm/tools/Harness/tests/fixtures/demo.c` is a deliberate
use-after-free in four tiny functions:

```c
char* make_buf(int n) { return (char*)malloc(n); }   // source: malloc ret
void fill(char* p) { memset(p, 0, 8); }
int use_after_free(void)
{
    char* b = make_buf(8);
    fill(b);
    free(b);
    return b[0];                                      // sink: load after free
}
int main(void) { return use_after_free(); }
```

(The file also contains a `long_ir`/`long_ir_helper` pair used by later
tutorials — ignore it for now.)

### 2. Compile it to LLVM IR

svf-harness analyzes LLVM bitcode/IR, not C. Compile with debug info and
named values so results map back to source:

```bash
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o /tmp/demo.ll svf-llvm/tools/Harness/tests/fixtures/demo.c
```

**Why these flags?** `-g` embeds the file/line info that fills the `loc`
field of every result — without it everything still works, but locations
degrade to empty strings. `-fno-discard-value-names` keeps your variable
names (`%b` instead of `%7`) in the IR, which the harness's name-based
anchors (Tutorial 03) rely on. `-O0` keeps the IR in obvious 1:1
correspondence with the source.

> Note: SVF writes preprocessed bitcode (`demo.ll.pre.svf.bc` etc.) **next to
> the input file** — analyze from a writable scratch directory like `/tmp`,
> not from a read-only checkout.

### 3. Start the daemon

```bash
Release-build/bin/svf-harness serve /tmp/demo.ll --socket /tmp/h.sock &
```

`serve` builds the whole analysis stack once — SVFIR (the program graph),
Andersen's points-to analysis, and the sparse value-flow graph (SVFG) — then
listens on the Unix socket for JSON-RPC queries. For this fixture that takes
well under a second; for real programs it can take minutes (the point of the
daemon: pay once, query many times). The socket file appears when the daemon
is ready.

### 4. Ask the first question: `summary`

```bash
Release-build/bin/svf-harness summary --socket /tmp/h.sock | python3 -m json.tool
```

Real output:

```json
{
    "functions": 9,
    "icfg_nodes": 93,
    "pag_nodes": 183,
    "svfg_nodes": 223
}
```

**Interpretation.** These are the sizes of the graphs the daemon just built:
9 functions (our 6 defined ones + external declarations like `malloc`),
93 interprocedural control-flow nodes (roughly one per IR instruction plus
call/return and entry/exit nodes), 183 program-assignment-graph variables
and objects, and 223 sparse value-flow nodes. `summary` is your sanity
check: if these numbers are zero or absurd, the wrong module was loaded.

### 5. List the functions

```bash
Release-build/bin/svf-harness functions --socket /tmp/h.sock | python3 -m json.tool
```

Real output (paths abbreviated):

```json
{
    "functions": [
        {
            "is_decl": false,
            "loc": {
                "file": ".../tests/fixtures/demo.c",
                "line": 5
            },
            "name": "fill",
            "num_args": 1
        },
        {
            "is_decl": true,
            "loc": {
                "file": "",
                "line": 0
            },
            "name": "free",
            "num_args": 1
        },
        {
            "is_decl": true,
            "loc": {
                "file": "",
                "line": 0
            },
            "name": "llvm.memset.p0.i64",
            "num_args": 4
        },
        ...
        {
            "is_decl": false,
            "loc": {
                "file": ".../tests/fixtures/demo.c",
                "line": 4
            },
            "name": "make_buf",
            "num_args": 1
        },
        {
            "is_decl": true,
            "loc": {
                "file": "",
                "line": 0
            },
            "name": "malloc",
            "num_args": 1
        },
        {
            "is_decl": false,
            "loc": {
                "file": ".../tests/fixtures/demo.c",
                "line": 6
            },
            "name": "use_after_free",
            "num_args": 0
        }
    ],
    "total": 9,
    "truncated": false
}
```

**Interpretation.** Three things to notice:

- **Why is `free`'s `loc` empty?** `free` and `malloc` are *external
  declarations* (`is_decl: true`): the module references them but does not
  contain their bodies, so there is no source location to report. The
  convention is uniform across the whole harness — `file: ""` / `line: 0`
  means "no source counterpart", never an error.
- **`llvm.memset.p0.i64`** is the compiler intrinsic clang lowered our
  `memset` call to. The analysis sees the program as LLVM does, so
  intrinsics show up as functions too.
- **`truncated: false`** — every list result carries this flag. `functions`
  caps at 200 entries; when a pattern matches more, you get the first 200
  and `truncated: true`, your cue to narrow the pattern instead of trusting
  the list to be complete.

### 6. Shut down

```bash
Release-build/bin/svf-harness shutdown --socket /tmp/h.sock
```

The daemon unlinks its socket and exits. (It also cleans up on
SIGINT/SIGTERM, but `shutdown` is the polite way.)

## What you learned

- The workflow: compile with `-g -O0 -fno-discard-value-names` → `serve` →
  query over the socket → `shutdown`.
- The daemon builds SVFIR → Andersen → SVFG once; queries are cheap
  afterwards.
- `loc.file == "" / line == 0` means "no source counterpart" (external
  declarations, synthetic nodes) — not a failure.
- Every list result carries `total` + `truncated`; respect the flag.

## Next

[Tutorial 02 — Exploring a program](02-exploring-a-program.md): the
`schema` method (the harness's self-description), regex function search,
call-graph navigation including indirect calls, and per-function CFGs.
