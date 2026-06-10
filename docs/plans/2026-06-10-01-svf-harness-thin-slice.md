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

- [ ] **Step 1: Failing test**

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

- [ ] **Step 2: Evidence.h**

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

- [ ] **Step 3: Implement `functions`** — iterate `callgraph` nodes, filter by
  `std::regex` on name, emit `{name, loc, is_decl, num_args}` per function (function
  object: `CallGraphNode::getFunction()`; verify type in `Graphs/CallGraph.h`).
  Support `--params` JSON in oneshot mode.

- [ ] **Step 4: Build + test PASS; Commit** — `harness: functions() with evidence locs`

## Phase 3: Daemon + socket protocol

### Task 3.1: HarnessServer + client mode + shutdown

**Files:**
- Create: `svf-llvm/tools/Harness/HarnessServer.h`, `HarnessServer.cpp`
- Modify: `svf-harness.cpp`, `CMakeLists.txt`
- Test: append to `tests/run_tests.py`

- [ ] **Step 1: Failing test (daemon lifecycle)**

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

- [ ] **Step 2: HarnessServer** — POSIX `AF_UNIX` stream socket: `socket/bind/listen`,
  accept loop, read one line per connection, parse JSON-RPC 2.0
  (`{jsonrpc,id,method,params}`), call `QueryEngine::dispatch`, reply
  `{"jsonrpc":"2.0","id":id,"result":...}` or `{"error":{"code":-32601|-32602|-32000,
  "message":...,"data":{"hint":...}}}`, `unlink` socket on exit. `shutdown` method
  breaks the loop. Register SIGINT/SIGTERM handlers to unlink the socket.

- [ ] **Step 3: Client mode in main** — every non-`serve` subcommand: build the same
  JSON-RPC request (params from `--params` JSON or method-specific flags added later),
  connect to `--socket` (default: `/tmp/svf-harness-$(sha1(abs bitcode paths or cwd))
  .sock` — for the default, clients use `--socket` or the `SVF_HARNESS_SOCKET` env
  var; keep resolution in one function), print result JSON, exit non-zero on error.
  Remove `--oneshot` test path? **No** — keep `--oneshot` permanently (CI-friendly,
  no socket needed).

- [ ] **Step 4: Build + test PASS; Commit** — `harness: daemon serve loop + JSON-RPC client mode`

## Phase 4: Schema + graph navigation

### Task 4.1: schema()

**Files:** Create `Schema.h/.cpp`; modify `QueryEngine.cpp`; test in `run_tests.py`.

- [ ] **Step 1: Failing test** — `schema()` returns `node_kinds` (≥ all ICFGNode +
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

- [ ] **Step 2: Implement** — a static registry built by hand (NOT reflection): node
  kinds from `ICFGNode::GNodeK`/`VFGNode::VFGNodeK` enums (grep `enum` in
  `Graphs/ICFGNode.h`, `Graphs/VFGNode.h`), each with 1-2 sentence English description
  and attribute list (`{kind,id,loc,ir}`); edge kinds (intra/call/ret for both graphs,
  call edges carry callsite); `methods` with JSON-schema-ish param descriptions. This
  hand-written registry IS the product: write descriptions an LLM can act on.

- [ ] **Step 3: PASS + Commit** — `harness: self-describing schema()`

### Task 4.2: callers / callees

- [ ] **Step 1: Failing test** — `callers("fill")` contains a callsite in
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

- [ ] **Step 2: Implement** via `CallGraph` node in/out edges; each edge result:
  `{caller, callee, callsite: <evidence node>, direct: bool}`. Unknown function name →
  JSON-RPC error with `hint` listing up to 5 closest names (use simple
  edit-distance over callgraph names).
- [ ] **Step 3: PASS + Commit** — `harness: call graph navigation with indirect calls`

### Task 4.3: cfg / defuse / pts / aliases

- [ ] **Step 1: Failing tests** — `cfg("use_after_free")` returns nodes with line
  numbers and intra edges; `pts` on variable `b` (resolve by `file:line` of the
  `make_buf` call) contains exactly one heap object whose loc points at the `malloc`
  line; `aliases` of `b` at least contains the `fill` parameter `p`; `defuse` of `b`
  includes the `free` callsite and the return load.
- [ ] **Step 2: Implement**
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
- [ ] **Step 3: PASS + Commit** — `harness: cfg/defuse/pts/aliases primitives`

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
