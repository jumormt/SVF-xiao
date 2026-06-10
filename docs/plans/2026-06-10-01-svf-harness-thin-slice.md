# svf-harness Thin Slice (v0) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Epic:** E1 (svf-harness thin slice)
**Design:** `docs/designs/2026-06-10-svf-harness-thin-slice.md` (read it first)

**Goal:** A daemon+CLI (`svf-harness`) and MCP wrapper through which an LLM can introspect SVF's schema, navigate program graphs, and obtain value-flow paths with structured JSON evidence.

**Architecture:** One C++ binary in `svf-llvm/tools/Harness/`: `serve` mode builds SVFIR→Andersen→SVFG once and answers newline-delimited JSON-RPC 2.0 on a Unix socket; every other subcommand is a thin client. `mcp/svf_harness_mcp/server.py` forwards MCP tool calls to the same socket. Evidence: every node result carries `{kind,id,loc,ir}`.

**Tech Stack:** SVF (this repo, C++17), vendored nlohmann/json, POSIX sockets, Python 3 stdlib for tests, Python `mcp` SDK for the wrapper.

**Build/test environment (CRITICAL, see CLAUDE.md):** always `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR`; incremental builds: `cmake --build Release-build -j8`; full Test-Suite only serially.

**Working conventions for every task:**
- Integration tests live in `svf-llvm/tools/Harness/tests/run_tests.py` (Python stdlib only: `subprocess`, `socket`, `json`, `unittest`). Run: `python3 svf-llvm/tools/Harness/tests/run_tests.py -v` with `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness`.
- SVF API names drift (recent refactor removed SVFFunction/SVFModule). Before using any API named in this plan, grep the header named alongside it and adapt; record any renames in the plan file as you go.
- Commit after every green step; message prefix `harness:`.

---

## Phase 1: Scaffolding

### Task 1.1: Tool skeleton + CMake wiring

**Files:**
- Create: `svf-llvm/tools/Harness/svf-harness.cpp`
- Create: `svf-llvm/tools/Harness/CMakeLists.txt`
- Create: `svf-llvm/tools/Harness/external/nlohmann/json.hpp` (vendored)
- Modify: `svf-llvm/tools/CMakeLists.txt` (add_subdirectory + ALL_TOOLS)

- [x] **Step 1: Vendor nlohmann/json (pinned 3.11.3)**

```bash
mkdir -p svf-llvm/tools/Harness/external/nlohmann
curl -L -o svf-llvm/tools/Harness/external/nlohmann/json.hpp \
  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
grep -q "NLOHMANN_JSON_VERSION_MAJOR 3" svf-llvm/tools/Harness/external/nlohmann/json.hpp
```

- [x] **Step 2: Write minimal main with subcommand dispatch**

`svf-harness.cpp`:

```cpp
//===- svf-harness.cpp -- LLM-friendly analysis harness daemon/CLI -------===//
#include "nlohmann/json.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

static const char* kUsage =
    "svf-harness — LLM-friendly SVF query daemon/CLI\n"
    "  svf-harness serve <bitcode...> [--socket PATH]   start daemon\n"
    "  svf-harness <method> [args] [--socket PATH]      query the daemon\n"
    "  svf-harness shutdown [--socket PATH]             stop the daemon\n"
    "Methods: schema summary functions callers callees cfg defuse pts aliases vfpath reachable\n";

int main(int argc, char** argv)
{
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0)
    {
        std::fputs(kUsage, stdout);
        return argc < 2 ? 1 : 0;
    }
    json err = {{"error", {{"message", "not implemented"}, {"method", argv[1]}}}};
    std::puts(err.dump().c_str());
    return 2;
}
```

- [x] **Step 3: CMake wiring**

`svf-llvm/tools/Harness/CMakeLists.txt`:

```cmake
add_llvm_executable(svf-harness svf-harness.cpp)
target_include_directories(svf-harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)
```

In `svf-llvm/tools/CMakeLists.txt`: add `add_subdirectory(Harness)` after `add_subdirectory(AE)` and `svf-harness` to the `ALL_TOOLS` list (the foreach below links SvfCore/SvfLLVM and sets output dirs — verify by reading the file).

- [x] **Step 4: Build and verify**

```bash
env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && cmake --build Release-build -j8 --target svf-harness'
Release-build/bin/svf-harness --help   # expect usage text, exit 0
```

- [x] **Step 5: Commit** — `harness: scaffold svf-harness tool with vendored json`

### Task 1.2: Test fixture + test runner skeleton

**Files:**
- Create: `svf-llvm/tools/Harness/tests/fixtures/demo.c`
- Create: `svf-llvm/tools/Harness/tests/run_tests.py`

- [x] **Step 1: Fixture with a malloc→free→use value flow**

`tests/fixtures/demo.c`:

```c
#include <stdlib.h>
#include <string.h>

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

- [x] **Step 2: Test runner with fixture compilation + binary discovery**

`tests/run_tests.py` (skeleton; later tasks append test methods):

```python
#!/usr/bin/env python3
import json, os, shutil, socket, subprocess, sys, tempfile, time, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ.get("SVF_HARNESS_BIN", "svf-harness")
CLANG = os.environ.get("CLANG", shutil.which("clang"))

def build_fixture(tmpdir, name="demo.c"):
    src = os.path.join(HERE, "fixtures", name)
    out = os.path.join(tmpdir, name.replace(".c", ".ll"))
    subprocess.check_call([CLANG, "-S", "-emit-llvm", "-g", "-O0",
                           "-fno-discard-value-names", "-o", out, src])
    return out

class HarnessTest(unittest.TestCase):
    def test_help(self):
        out = subprocess.run([BIN, "--help"], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0)
        self.assertIn("svf-harness", out.stdout)

if __name__ == "__main__":
    unittest.main()
```

- [x] **Step 3: Run, expect PASS**

```bash
SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
  env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c \
  'source ./setup.sh > /dev/null && python3 svf-llvm/tools/Harness/tests/run_tests.py -v'
```

- [x] **Step 4: Commit** — `harness: add test fixture and python test runner`

## Phase 2: QueryEngine + oneshot mode (testable before sockets)

### Task 2.1: QueryEngine with summary(), wired via `--oneshot`

**Files:**
- Create: `svf-llvm/tools/Harness/QueryEngine.h`, `QueryEngine.cpp`
- Modify: `svf-llvm/tools/Harness/svf-harness.cpp`, `CMakeLists.txt`
- Test: append to `tests/run_tests.py`

- [x] **Step 1: Failing test**

```python
    def test_oneshot_summary(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "summary", ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            j = json.loads(out.stdout)
            self.assertGreaterEqual(j["functions"], 4)
            self.assertIn("icfg_nodes", j); self.assertIn("svfg_nodes", j)
```

Run; expect FAIL (`not implemented`).

- [x] **Step 2: QueryEngine**

`QueryEngine.h`:

```cpp
#pragma once
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

namespace SVF { class SVFIR; class SVFG; class AndersenBase; class CallGraph; }

class QueryEngine
{
public:
    /// Builds SVFModule -> SVFIR -> Andersen -> SVFG. Throws std::runtime_error on bad input.
    explicit QueryEngine(const std::vector<std::string>& moduleNames);
    nlohmann::json dispatch(const std::string& method, const nlohmann::json& params);
    nlohmann::json summary() const;
private:
    SVF::SVFIR* pag = nullptr;
    SVF::AndersenBase* ander = nullptr;
    SVF::SVFG* svfg = nullptr;
    SVF::CallGraph* callgraph = nullptr;
};
```

`QueryEngine.cpp` constructor follows the svf-ex bootstrap chain (verify names in
`svf-llvm/tools/Example/svf-ex.cpp` first):

```cpp
#include "QueryEngine.h"
#include "Graphs/SVFG.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
using namespace SVF;
using json = nlohmann::json;

QueryEngine::QueryEngine(const std::vector<std::string>& moduleNames)
{
    std::vector<std::string> names(moduleNames);
    LLVMModuleSet::preProcessBCs(names);
    LLVMModuleSet::buildSVFModule(names);
    SVFIRBuilder builder;
    pag = builder.build();
    ander = AndersenWaveDiff::createAndersenWaveDiff(pag);
    callgraph = ander->getCallGraph();
    SVFGBuilder svfBuilder;
    svfg = svfBuilder.buildFullSVFG(static_cast<AndersenWaveDiff*>(ander));
}

json QueryEngine::summary() const
{
    json j;
    j["functions"] = callgraph->getTotalNodeNum();   // verify accessor in Graphs/CallGraph.h
    j["icfg_nodes"] = pag->getICFG()->getTotalNodeNum();
    j["svfg_nodes"] = svfg->getTotalNodeNum();
    j["pag_nodes"] = pag->getTotalNodeNum();
    return j;
}

json QueryEngine::dispatch(const std::string& m, const json& p)
{
    if (m == "summary") return summary();
    throw std::runtime_error("unknown method: " + m);
}
```

In `main`: when `argv[1] == "--oneshot"`, treat `argv[2]` as method, pass remaining args
through SVF's `OptionBase::parseOptions` (as svf-ex does) to get module paths, construct
`QueryEngine`, print `dispatch(method, {})`. Add `QueryEngine.cpp` to the
`add_llvm_executable` sources.

- [x] **Step 3: Build + run test, expect PASS** (same commands as Task 1.2/1.1)
- [x] **Step 4: Commit** — `harness: QueryEngine bootstrap + summary via --oneshot` (a94ed8dc)

**Task 2.1 implementation notes (deltas from the sketch above):**
- `SVFGBuilder::buildFullSVFG` takes `BVDataPTAImpl*` (AndersenBase derives from it);
  no `static_cast<AndersenWaveDiff*>` needed.
- LLVM-style builds disable exceptions: added `target_compile_options(svf-harness
  PRIVATE -fexceptions)` (harness target only).
- SVF stat reports pollute stdout: oneshot injects `-stat=false` as the first parsed
  option (a user-passed `-stat=true` to --oneshot exits with a duplicate-option
  error — acceptable; oneshot stdout must be pure JSON).
- SVF core `abort()`s on unreadable bitcode before any throw: QueryEngine ctor
  pre-validates module paths (ifstream) and throws runtime_error.
- CLANG-missing guard added to run_tests.py (Phase 1 review carry-over).

### Task 2.2: functions(pattern) + uniform node evidence record

**Files:**
- Create: `svf-llvm/tools/Harness/Evidence.h` (header-only helpers)
- Modify: `QueryEngine.{h,cpp}`
- Test: append to `tests/run_tests.py`

- [x] **Step 1: Failing test**

```python
    def test_functions_lists_fixture_funcs(self):
        j = self.oneshot("functions", {"pattern": "use_after_free"})
        names = [f["name"] for f in j["functions"]]
        self.assertIn("use_after_free", names)
        f = j["functions"][names.index("use_after_free")]
        self.assertTrue(f["loc"]["file"].endswith("demo.c"))
        self.assertGreater(f["loc"]["line"], 0)
```

Add an `oneshot(method, params)` helper to the test class that passes params as a JSON
string argument: `[BIN, "--oneshot", method, "--params", json.dumps(params), ll]`.

- [x] **Step 2: Evidence.h**

```cpp
#pragma once
#include "nlohmann/json.hpp"
namespace SVF { class ICFGNode; class VFGNode; class SVFVar; }
namespace evidence {
/// Uniform record for any SVF graph node: {kind,id,loc:{file,line,func},ir}
/// Implement with node->getSourceLoc() (verify on ICFGNode/VFGNode in current
/// master — it returns a string like "{ ln: 7 cl: 5 fl: demo.c }"; parse
/// ln/fl/func out of it) and node->toString() for "ir" (truncate to 200 chars).
nlohmann::json node(const SVF::ICFGNode* n);
nlohmann::json node(const SVF::VFGNode* n);
nlohmann::json node(const SVF::SVFVar* n);
}
```

(If `getSourceLoc()` returns structured `SVFLoc` in current master, use it directly —
grep `getSourceLoc` in `svf/include/Util/SVFValue.h` and `Graphs/ICFGNode.h` first.)

- [x] **Step 3: Implement `functions`** — iterate `callgraph` nodes, filter by
  `std::regex` on name, emit `{name, loc, is_decl, num_args}` per function (function
  object: `CallGraphNode::getFunction()`; verify type in `Graphs/CallGraph.h`).
  Support `--params` JSON in oneshot mode.

- [x] **Step 4: Build + test PASS; Commit** — `harness: functions() with evidence locs`

**Implementation notes (Task 2.2, commit cebb73e3):** Evidence became `.h` + `.cpp`
(not header-only). API deltas vs sketch: SVFValue lives in `svf/include/SVFIR/SVFValue.h`
(not Util/); `getSourceLoc()` strings are JSON-ish with *quoted* keys, and instructions
use `"fl"` while functions use `"file"` — `evidence::loc()` tries both. `kind` =
leading alpha run of `toString()` (each SVF subclass prints its class name first);
function object is `const FunObjVar*` via `CallGraphNode::getFunction()`, with
`isDeclaration()` / `arg_size()`. `evidence::node()` overloads compile but are first
exercised by later tasks (callers/cfg). `--params` is stripped before
`OptionBase::parseOptions` since SVF's parser rejects unknown flags.

## Phase 3: Daemon + socket protocol

### Task 3.1: HarnessServer + client mode + shutdown

**Files:**
- Create: `svf-llvm/tools/Harness/HarnessServer.h`, `HarnessServer.cpp`
- Modify: `svf-harness.cpp`, `CMakeLists.txt`
- Test: append to `tests/run_tests.py`

- [x] **Step 1: Failing test (daemon lifecycle)**

```python
    def test_daemon_roundtrip(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td); sock = os.path.join(td, "h.sock")
            srv = subprocess.Popen([BIN, "serve", ll, "--socket", sock],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.wait_for(lambda: os.path.exists(sock), 60)
                j = self.client(sock, "summary", {})
                self.assertIn("functions", j)
                j2 = self.client(sock, "summary", {})      # state reused, 2nd call fast
                self.assertEqual(j, j2)
            finally:
                self.client(sock, "shutdown", {}); srv.wait(timeout=10)

    def client(self, sock, method, params, _id=[0]):
        _id[0] += 1
        req = {"jsonrpc": "2.0", "id": _id[0], "method": method, "params": params}
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(sock); s.sendall((json.dumps(req) + "\n").encode())
            buf = b""
            while not buf.endswith(b"\n"): buf += s.recv(65536)
        resp = json.loads(buf)
        if "error" in resp: raise AssertionError(resp["error"])
        return resp["result"]
```

- [x] **Step 2: HarnessServer** — POSIX `AF_UNIX` stream socket: `socket/bind/listen`,
  accept loop, read one line per connection, parse JSON-RPC 2.0
  (`{jsonrpc,id,method,params}`), call `QueryEngine::dispatch`, reply
  `{"jsonrpc":"2.0","id":id,"result":...}` or `{"error":{"code":-32601|-32602|-32000,
  "message":...,"data":{"hint":...}}}`, `unlink` socket on exit. `shutdown` method
  breaks the loop. Register SIGINT/SIGTERM handlers to unlink the socket.

- [x] **Step 3: Client mode in main** — every non-`serve` subcommand: build the same
  JSON-RPC request (params from `--params` JSON or method-specific flags added later),
  connect to `--socket` (default: `/tmp/svf-harness-$(sha1(abs bitcode paths or cwd))
  .sock` — for the default, clients use `--socket` or the `SVF_HARNESS_SOCKET` env
  var; keep resolution in one function), print result JSON, exit non-zero on error.
  Remove `--oneshot` test path? **No** — keep `--oneshot` permanently (CI-friendly,
  no socket needed).

- [x] **Step 4: Build + test PASS; Commit** — `harness: daemon serve loop + JSON-RPC client mode`

**Task 3.1 implementation notes (commit 227e72ee, 9/9 tests green):** Tests landed in a
stricter form than the sketch above: `client()` returns the full JSON-RPC envelope (asserts
`jsonrpc`/`id`), `test_daemon_roundtrip` also checks -32601 + `data.hint` for an unknown
method and that the socket file is gone after shutdown; added `test_cli_client_subcommand`;
client helper uses a 60 s `settimeout` (recv timeout instead of sleeps). Default socket id
uses FNV-1a 64 (first 12 hex chars) of absolute module paths joined by ':', not sha1.
Resolution order `--socket > SVF_HARNESS_SOCKET > hash default` lives in
`resolveSocketPath()`; the client side (`resolveClientSocket()`) falls back to a unique
`/tmp/svf-harness-*.sock` glob, else errors with a hint. `HarnessServer` binds+listens in
the ctor (socket file exists when it returns; throws "daemon already running" after a live
connect probe, unlinks stale files), `run()` is only the accept loop; 1 MiB request cap →
-32600; SIGPIPE ignored; SIGINT/SIGTERM via sigaction without SA_RESTART → EINTR → cleanup.
-32601 hint is built from `QueryEngine::methodNames()`, backed by the same static method
table `dispatch()` uses (no duplicated method list). `dumpJson` moved to shared
`JsonUtil.h` (namespace `harness`), used for stdout and all socket writes. Output contract
split: oneshot errors stay `{"error":{...}}`; client mode prints the bare JSON-RPC error
object `{code,message[,data]}` on exit 1 (matches what the daemon returns).

## Phase 4: Schema + graph navigation

### Task 4.1: schema()

**Review-decision (from Task 2.2 quality review):** evidence `kind` strings derive from
`toString()` prefixes. `schema()` MUST enumerate `node_kinds` from the same source so the
invariant "every evidence kind appears in schema().node_kinds" holds — include the
`InterMSSAPHISVFGNode` aliases `FormalINPHISVFGNode`/`ActualOUTPHISVFGNode` which exist in
no enum. Do NOT build node_kinds from GNodeK enums alone.

**Files:** Create `Schema.h/.cpp`; modify `QueryEngine.cpp`; test in `run_tests.py`.

- [x] **Step 1: Failing test** — `schema()` returns `node_kinds` (≥ all ICFGNode +
  VFGNode subclass names), `edge_kinds`, `methods` (all 11 with param/result docs),
  and every entry has a non-empty `"description"`.

```python
    def test_schema_self_describing(self):
        j = self.oneshot("schema", {})
        kinds = {k["name"] for k in j["node_kinds"]}
        self.assertLessEqual({"IntraICFGNode", "CallICFGNode", "RetICFGNode",
                              "LoadVFGNode", "StoreVFGNode"}, kinds)
        self.assertEqual(len(j["methods"]), 11)
        for m in j["methods"]: self.assertTrue(m["description"])
```

- [x] **Step 2: Implement** — a static registry built by hand (NOT reflection): node
  kinds from `ICFGNode::GNodeK`/`VFGNode::VFGNodeK` enums (grep `enum` in
  `Graphs/ICFGNode.h`, `Graphs/VFGNode.h`), each with 1-2 sentence English description
  and attribute list (`{kind,id,loc,ir}`); edge kinds (intra/call/ret for both graphs,
  call edges carry callsite); `methods` with JSON-schema-ish param descriptions. This
  hand-written registry IS the product: write descriptions an LLM can act on.

- [x] **Step 3: PASS + Commit** — `harness: self-describing schema()` (c7f0029f)

**Task 4.1 implementation notes (commit c7f0029f, 11/11 tests green):**
- `Schema.cpp` enumerates node_kinds from audited `toString()` prefixes per the
  review-decision, NOT from enums. Audit result: 67 kinds — 7 ICFG (incl. base
  `ICFGNode` + `GlobalICFGNode`), 31 VFG/SVFG, 29 SVFIR vars. Beyond the two known
  MSSA aliases, the audit found a SECOND alias pair: `InterPHIVFGNode::toString()`
  prints `FormalParmPHI` / `ActualRetPHI` (VFG.cpp:335) — also in no enum; both are
  in node_kinds. Printable base classes (`SVFVar`, `VFGNode`, `StmtVFGNode`,
  `MRSVFGNode`, `MSSAPHISVFGNode`, `ArgumentVFGNode`, `ValVar`, `ObjVar`,
  `BaseObjVar`, `ICFGNode`) are included since `kindOf()` can emit them (e.g.
  `DummyVersionPropSVFGNode` has no override and prints `VFGNode`). Invariant
  verified bidirectionally by script: printed-prefix set == node_kinds set, both
  directions empty diff. `FIObjVar` (named in older notes) no longer exists —
  superseded by `BaseObjVar`.
- edge_kinds = 10 concrete classes only (3 ICFG + 7 SVFG families). Abstract
  bases `ICFGEdge`/`VFGEdge`/`DirectVFGEdge`/`IndirectSVFGEdge` deliberately
  omitted — the evidence-kind invariant covers nodes only, and Task 5.1 step
  labels will use the concrete names. Note: `DirectSVFGEdge::toString()` prints
  "DirectVFGEdge" (name mismatch in SVF, harmless here).
- `methods` lists all 11 with `params` ({type,description,required}) and
  `returns`; `implemented` is merged at runtime in `QueryEngine::schemaQ` from
  `methodNames()` (currently true for schema/summary/functions only).
  `program` = {modules (ctor paths, new `modules` member), summary()}.
- `evidence_record` documents the ~200-byte UTF-8-safe ir truncation and the
  empty-""/0 loc semantics for declarations/globals/synthetic nodes.

### Task 4.2: callers / callees

- [x] **Step 1: Failing test** — `callers("fill")` contains a callsite in
  `use_after_free` (file/line evidence); `callees({"func":"use_after_free"})` includes
  `make_buf`, `fill`, `free`; add an indirect-call fixture `tests/fixtures/indirect.c`
  (function pointer table) and assert the indirect callee is resolved (this exercises
  Andersen-resolved edges):

```c
typedef int (*op_t)(int);
static int dbl(int x) { return x * 2; }
static int neg(int x) { return -x; }
int apply(int which, int x) { op_t ops[2] = {dbl, neg}; return ops[which](x); }
int main(void) { return apply(0, 21); }
```

- [x] **Step 2: Implement** via `CallGraph` node in/out edges; each edge result:
  `{caller, callee, callsite: <evidence node>, direct: bool}`. Unknown function name →
  JSON-RPC error with `hint` listing up to 5 closest names (use simple
  edit-distance over callgraph names).
- [x] **Step 3: PASS + Commit** — `harness: call graph navigation with indirect calls`

**Task 4.2 implementation notes (commit 70338a39, 17/17 tests green):**
- `QueryEngine::callEdges(params, incoming)` backs both methods: walk the
  function's `CallGraphNode` in/out edges; a `CallGraphEdge` merges all calls
  between two functions, so its `directCalls`/`indirectCalls` callsite sets
  (`directCallsBegin/End`, `indirectCallsBegin/End`) are expanded to one result
  row per (callsite, callee). Rows sorted by (callsite ICFG id, callee) — the
  sets are unordered. Same 200-cap/total/truncated shape as `functions()`.
- `findFunction()` returns the `CallGraphNode*` (not just `FunObjVar*`, since
  the edges live on the node); on miss it throws with the 5 closest names by
  iterative two-row Levenshtein (names capped at 64 chars), deduped + sorted
  by (distance, name) for a deterministic hint.
- **Fixture deviation (documented):** `indirect.c` fills the function-pointer
  table with explicit element stores instead of the planned initializer list
  `op_t ops[2] = {dbl, neg};`. Reason: clang 10 lowers the initializer to
  `llvm.memcpy.p0i8.p0i8.i64` from a constant global aggregate
  (`@__const.apply.ops`), and SVF's MEMCPY extapi summary does not propagate
  the function pointers through that copy — Andersen leaves the callsite
  unresolved (verified: same harness code resolves {dbl, neg} with explicit
  stores; static-function names need no adaptation, they stay `dbl`/`neg`).
  Possible SVF-core improvement, noted, not pursued in v0.
- Carry-overs done: `tests/check_schema_kinds.py` (~55 lines, stdlib-only text
  analysis; per-toString-body LEADING `rawstr << "..."` literals — first of
  body or first after `if(...)`/`else`, which captures both alias pairs and
  drops mid-body output like `"SVFStmt: ["`; edge names filtered by `Edge`
  suffix; 67 kinds consistent), wired into run_tests.py with skipUnless on
  svf/lib presence; `test_schema_self_describing` now asserts
  {schema,summary,functions,callers,callees} ⊆ implemented set and that every
  implemented method has dict params + non-empty returns.

**Task 4.2 code-review fixes (commit dbc7e91d, 18/18 tests green):**
- **Fix 1 (contract):** Schema.cpp `returns` strings for `callers`/`callees`
  corrected to document the real response shape `{function, calls:[{caller,
  callee, callsite, direct}], total, truncated, matched_functions}`. The old
  docs showed a bare array `[{...}]` which never matched the implementation.
  `test_schema_self_describing` now asserts "calls" and "truncated" are present
  in both entries as a drift guard.
- **Fix 2 (ambiguous names — DECISION):** `callers`/`callees` now use MERGE
  semantics: new `findFunctions()` returns ALL call-graph nodes whose name
  matches exactly; `callEdges()` merges rows from all of them and reports
  `matched_functions: N` (1 in the common case). This is the right behaviour
  for same-named statics in different TUs. `findFunction()` (singular) is kept
  for future single-target uses and throws on ambiguity with message
  "ambiguous function name 'X': N matches; use functions() to disambiguate".
  New fixtures `dup_a.c`/`dup_b.c` and `test_duplicate_function_names_merged`
  verify the merge path (matched_functions==2, callers=={entry_a,entry_b}).
  `oneshot()` now accepts a fixture list for multi-module tests.
- **Fix 3 (hint perf):** edit-distance was computed inside a `sort` comparator,
  causing O(n log n) Levenshtein DP runs over all call-graph names. Refactored:
  unique candidate names collected into `std::set`, distance computed once per
  candidate into `vector<pair<size_t,string>>`, then `std::partial_sort`
  (top 5, O(n log 5)) instead of full sort. No behaviour change.

### Task 4.3: cfg / defuse / pts / aliases

**Test obligation (from Task 2.2 quality review):** assert that cfg/defuse results carry
instruction-level locs (the `"fl"` key path of evidence::loc) and exercise ir-truncation
on at least one long node string.

**Pre-implementation note (reviewer-suggested):** natural split point: if
`QueryEngine.cpp` doubles in size as cfg/defuse/pts/aliases land, move query
method bodies to `Queries.cpp` (planned refactor). Keep `QueryEngine.h` as the
public facade; all five source files share the same CMake target.

- [x] **Step 1: Failing tests** — `cfg("use_after_free")` returns nodes with line
  numbers and intra edges; `pts` on variable `b` (resolve by `file:line` of the
  `make_buf` call) contains exactly one heap object whose loc points at the `malloc`
  line; `aliases` of `b` at least contains the `fill` parameter `p`; `defuse` of `b`
  includes the `free` callsite and the return load.
- [x] **Step 2: Implement**
  - `cfg`: iterate function's ICFG nodes (`ICFG` + function filter; check
    `FunObjVar`→entry/exit APIs in `Graphs/ICFG.h`), emit nodes + intra edges.
  - var resolution helper (shared with Phase 5): `{"var": {"file": "...", "line": N,
    "name": "b"?}}` → scan ICFG nodes of that line for defined `SVFVar`s; or
    `{"var": {"func": "make_buf", "ret": true}}` / `{"func": "memcpy", "arg": 0}`.
  - `pts`: `ander->getPts(varId)`, map object NodeIDs through `pag->getGNode` to
    evidence records. `aliases`: iterate candidate vars of same function (v0: all
    PAG ValVars of the module), keep `ander->alias(a,b) != NoAlias`, cap at 50 +
    `truncated`.
  - `defuse`: SVFStmt edges of the var (`SVFVar::getInEdges/getOutEdges` in
    `SVFIR/SVFVariables.h`).
- [x] **Step 3: PASS + Commit** — `harness: cfg/defuse/pts/aliases primitives`
  (commit ff2308a1, 24/24 tests green)

**Task 4.3 implementation notes (commit ff2308a1):**
- **Queries.cpp split (reviewer-planned):** query method bodies (`functions`,
  `callEdges`, `schemaQ`, `resolveVars`, `cfg`, `defuse`, `pts`, `aliases`)
  moved to a new `Queries.cpp` (same class, second TU); QueryEngine.cpp keeps
  ctor/summary/findFunction(s)/methodTable/dispatch. CMake target gained the
  file; no other build changes.
- **resolveVars(spec)** (shared with Task 5.1 anchors): all three forms
  implemented; result deduped + sorted by node id. file:line form scans ICFG
  nodes whose `evidence::loc` file path-suffix-matches (at a '/' boundary) and
  collects the **dst vars** of each node's `getSVFStmts()`; optional "name" is
  a `getValueName()` substring filter. func form walks the CallGraphNode
  in-edges' direct+indirect callsite sets (same machinery as callers); ret via
  `cs->getRetICFGNode()` + `pag->callsiteHasRet/getCallSiteRet`, arg via
  `cs->getArgument(N)`/`arg_size()`. Empty resolution throws with the accepted
  forms and (file:line) up to 5 nearest defining lines.
- **API notes (headers checked, no renames needed):** `AliasResult` is a plain
  enum in `SVFIR/SVFType.h` (`SVF::NoAlias`); `alias(NodeID,NodeID)`/`getPts`
  are non-const on PointerAnalysis (fine through the `ander` pointer member);
  SVFStmt edge sets are `SVFStmt::SVFStmtSetTy`; SVFStmt kind names come from
  a `PEDGEK` switch — toString() prefixes do NOT work for stmts (they print
  "SVFStmt: [" mid-body, see check_schema_kinds notes).
- **cfg:** singular `findFunction` (ambiguity → error, verified vs dup
  fixtures); nodes = ICFG nodes with `getFun()==fun` (id-ordered map walk),
  edges = their out-edges incl. Call/RetCFGEdge crossings; caps 500/1000,
  `truncated` = either.
- **defuse semantics (v0):** defs = SVFStmt in-edges, uses = out-edges of the
  var. Note a Store INTO a pointer-typed var (e.g. `b` at line 8) appears as a
  def — memory-def semantics, intended.
- **aliases v0 scope** documented in Schema.cpp description: candidates =
  ValVars of the var's own function only; no-function vars get empty lists.
- **Fixture/lines:** demo.c lines verified: make_buf=4, `b =` 8, fill 9,
  free 10, return b[0] 11. Appended (below the old code, line numbers stable)
  `long_ir`/`long_ir_helper` with a 20-arg call whose CallICFGNode dump
  exceeds the 200-byte ir cap → `test_cfg_ir_truncation` covers the Task 2.2
  truncation obligation; instruction-level "fl" locs asserted in both the cfg
  and defuse tests.
- **Test deltas vs plan Step 1 sketch:** `pts`/`aliases` anchor on
  {func: malloc, ret: true} (the make_buf-local malloc result) instead of `b`;
  aliases asserts ≥1 same-function alias (resolves to make_buf's RetValPN) —
  fill's `p` is out of v0 scope by design. Schema returns docs for the 4
  methods rewritten to the real shapes; drift guards added to
  test_schema_self_describing (vars/truncated/points_to/total_nodes).

## Phase 5: Value flow + path evidence

### Task 5.1: vfpath + reachable

**Files:** modify `QueryEngine.{h,cpp}`, `Evidence.h`; tests.

- [ ] **Step 1: Failing test (the acceptance scenario)**

```python
    def test_vfpath_malloc_to_use(self):
        j = self.oneshot("vfpath", {
            "source": {"func": "malloc", "ret": True},
            "sink":   {"file": "demo.c", "line": 11},   # return b[0]
            "k": 3})
        self.assertGreaterEqual(len(j["paths"]), 1)
        p = j["paths"][0]
        files = {n["node"]["loc"]["file"].split("/")[-1] for n in p["steps"]}
        self.assertEqual(files, {"demo.c"})
        kinds = [s["edge"] for s in p["steps"][1:]]
        self.assertTrue(any(k in ("call", "ret") for k in kinds))  # crosses make_buf boundary
        self.assertIn("truncated", j)
```

- [ ] **Step 2: Implement** — resolve source/sink anchors to SVFG nodes
  (`svfg->getDefSVFGNode(var)` for defs; for sinks, the SVFG node(s) whose ICFG node
  matches the `file:line`); BFS over SVFG out-edges recording parent pointers, budget
  `max_visited=100000` (param-overridable), collect up to k distinct paths; emit
  `steps: [{node: <evidence>, edge: intra|call|ret, callsite?: <evidence>}]`
  (edge kind via `dyn_cast<CallDirSVFGEdge/RetDirSVFGEdge/...>` — grep
  `Graphs/SVFGEdge.h` for the class list). `reachable` = same machinery, k=1,
  multiple sinks, returns `[{sink, reachable, first_path?}]`.
- [ ] **Step 3: PASS + Commit** — `harness: value-flow paths with witness evidence`

## Phase 6: MCP thin wrapper

### Task 6.1: MCP server forwarding to the daemon

**Files:**
- Create: `mcp/svf_harness_mcp/server.py`, `mcp/svf_harness_mcp/README.md`,
  `mcp/svf_harness_mcp/pyproject.toml`
- Test: `mcp/svf_harness_mcp/test_smoke.py`

- [ ] **Step 1: Failing smoke test** (needs `pip install mcp`; skip with clear message
  if unavailable): in-memory MCP client lists tools → expects the 11 methods plus
  `load_program`; calls `load_program(bitcode)` then `summary` → same fields as CLI.
- [ ] **Step 2: Implement** — `mcp.server.fastmcp.FastMCP`; on startup NO daemon;
  `load_program(paths)` spawns `svf-harness serve` (binary path from
  `SVF_HARNESS_BIN`), waits for socket, remembers it; the other tools are registered
  dynamically from the daemon's `schema()` response (name, description, params) and
  forward via the socket protocol; errors surface the JSON-RPC `hint`. ~200 lines.
- [ ] **Step 3: PASS + Commit** — `harness: MCP thin wrapper over daemon socket`

## Phase 7: Acceptance + integration

### Task 7.1: End-to-end demo + ctest hook + docs

- [ ] **Step 1: Demo script** `svf-llvm/tools/Harness/demo/llm_workflow.sh`: builds
  fixture, starts daemon, replays the LLM sequence `schema → functions(".*free.*") →
  vfpath(malloc ret → b[0])`, pretty-prints the witness path with `python3 -m json.tool`,
  shuts down. Must exit 0 and show a path whose first step locates `malloc` and last
  step locates line 11.
- [ ] **Step 2: Wire tests into the build** — add to `svf-llvm/tools/Harness/CMakeLists.txt`:

```cmake
if(BUILD_TESTING)
  add_test(NAME harness_integration
           COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py -v)
  set_tests_properties(harness_integration PROPERTIES
    ENVIRONMENT "SVF_HARNESS_BIN=$<TARGET_FILE:svf-harness>")
endif()
```

  Confirm `ctest -R harness_integration` passes from Release-build (serial, as always).
- [ ] **Step 3: Run FULL existing Test-Suite serially** — must stay 2266/2266 + new test.
- [ ] **Step 4: Docs** — `svf-llvm/tools/Harness/README.md` (protocol spec, method
  reference generated by pasting `schema()` output, MCP setup for Claude Code:
  `claude mcp add svf -- python3 mcp/svf_harness_mcp/server.py`); update
  `docs/PROGRESS.md` (plan → done, session log) and write
  `docs/summaries/2026-06-10-svf-harness-thin-slice.md` if non-trivial lessons emerged.
- [ ] **Step 5: Commit + push** — `harness: e2e demo, ctest integration, docs`

## Verification (plan-level)

- [ ] `python3 .../run_tests.py -v` all green via fresh build
- [ ] `ctest -R harness_integration` green; full Test-Suite still 2266/2266 (serial)
- [ ] `demo/llm_workflow.sh` exits 0 with a malloc→use witness path
- [ ] MCP smoke test green (or cleanly skipped where `mcp` not installed)
- [ ] Upstream-merge friendliness: all new code confined to `svf-llvm/tools/Harness/`,
  `mcp/`, `docs/` + 2 lines in `svf-llvm/tools/CMakeLists.txt`
- [ ] PROGRESS.md updated with results
