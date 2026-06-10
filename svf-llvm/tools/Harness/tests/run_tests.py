#!/usr/bin/env python3
import json, os, shutil, socket, subprocess, sys, tempfile, time, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ.get("SVF_HARNESS_BIN", "svf-harness")
CLANG = os.environ.get("CLANG", shutil.which("clang"))
if not CLANG:
    sys.exit("error: clang not found on PATH; set CLANG=/path/to/clang")

def build_fixture(tmpdir, name="demo.c"):
    src = os.path.join(HERE, "fixtures", name)
    out = os.path.join(tmpdir, name.replace(".c", ".ll"))
    subprocess.check_call([CLANG, "-S", "-emit-llvm", "-g", "-O0",
                           "-fno-discard-value-names", "-o", out, src])
    return out

def build_fixtures(tmpdir, names):
    """Build multiple fixtures; returns list of .ll paths in the same order."""
    return [build_fixture(tmpdir, name) for name in names]

class HarnessTest(unittest.TestCase):
    def test_help(self):
        out = subprocess.run([BIN, "--help"], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0)
        self.assertIn("svf-harness", out.stdout)

    def test_fixture_compiles(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            self.assertTrue(os.path.exists(ll))
            with open(ll) as f: text = f.read()
            self.assertIn("use_after_free", text)
            self.assertIn("malloc", text)

    def test_oneshot_summary(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "summary", ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            j = json.loads(out.stdout)
            self.assertGreaterEqual(j["functions"], 4)
            self.assertIn("icfg_nodes", j); self.assertIn("svfg_nodes", j)

    def oneshot(self, method, params, fixture="demo.c"):
        """Run a one-shot query.

        ``fixture`` may be a single filename (str) or a list of filenames for
        multi-module tests.  All fixtures are compiled and passed to svf-harness
        together.
        """
        with tempfile.TemporaryDirectory() as td:
            if isinstance(fixture, list):
                lls = build_fixtures(td, fixture)
            else:
                lls = [build_fixture(td, fixture)]
            out = subprocess.run([BIN, "--oneshot", method, "--params",
                                  json.dumps(params)] + lls,
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, f"stderr={out.stderr} stdout={out.stdout}")
            return json.loads(out.stdout)

    def test_functions_lists_fixture_funcs(self):
        j = self.oneshot("functions", {"pattern": "use_after_free"})
        names = [f["name"] for f in j["functions"]]
        self.assertIn("use_after_free", names)
        f = j["functions"][names.index("use_after_free")]
        self.assertTrue(f["loc"]["file"].endswith("demo.c"))
        self.assertGreater(f["loc"]["line"], 0)

    def test_oneshot_invalid_ir_json_error(self):
        with tempfile.TemporaryDirectory() as td:
            bad = os.path.join(td, "garbage.ll")
            with open(bad, "w") as f:
                f.write("this is not llvm ir\n")
            out = subprocess.run([BIN, "--oneshot", "summary", bad],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1, f"rc={out.returncode} stderr={out.stderr}")
            j = json.loads(out.stdout)
            self.assertEqual(j["error"]["code"], -32000)
            self.assertIn("not an LLVM IR file", j["error"]["message"])

    def test_params_flag_missing_value(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "functions", ll, "--params"],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            j = json.loads(out.stdout)
            self.assertEqual(j["error"]["code"], -32000)
            self.assertIn("--params", j["error"]["message"])

    def test_functions_reports_total(self):
        j = self.oneshot("functions", {})
        self.assertEqual(j["total"], len(j["functions"]))
        self.assertFalse(j["truncated"])

    def wait_for(self, cond, timeout):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if cond(): return
            time.sleep(0.1)
        self.fail("timeout waiting for condition")

    def client(self, sock, method, params, _id=[0]):
        _id[0] += 1
        req = {"jsonrpc": "2.0", "id": _id[0], "method": method, "params": params}
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(60)
            s.connect(sock); s.sendall((json.dumps(req) + "\n").encode())
            buf = b""
            while not buf.endswith(b"\n"):
                chunk = s.recv(65536)
                if not chunk: break
                buf += chunk
        resp = json.loads(buf)
        self.assertEqual(resp.get("jsonrpc"), "2.0")
        self.assertEqual(resp.get("id"), _id[0])
        return resp

    def test_daemon_roundtrip(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td); sock = os.path.join(td, "h.sock")
            srv = subprocess.Popen([BIN, "serve", ll, "--socket", sock],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.wait_for(lambda: os.path.exists(sock), 60)
                r = self.client(sock, "summary", {})
                self.assertIn("functions", r["result"])
                r2 = self.client(sock, "functions", {"pattern": "make"})
                self.assertEqual(r2["result"]["functions"][0]["name"], "make_buf")
                bad = self.client(sock, "nope", {})
                self.assertEqual(bad["error"]["code"], -32601)
                self.assertIn("hint", bad["error"].get("data", {}))
            finally:
                self.client(sock, "shutdown", {})
                srv.wait(timeout=10)
                self.assertFalse(os.path.exists(sock))

    def test_cli_client_subcommand(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td); sock = os.path.join(td, "h.sock")
            srv = subprocess.Popen([BIN, "serve", ll, "--socket", sock],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.wait_for(lambda: os.path.exists(sock), 60)
                out = subprocess.run([BIN, "functions", "--params",
                                      json.dumps({"pattern": "main"}),
                                      "--socket", sock],
                                     capture_output=True, text=True)
                self.assertEqual(out.returncode, 0, out.stderr)
                j = json.loads(out.stdout)
                self.assertEqual(j["functions"][0]["name"], "main")
            finally:
                subprocess.run([BIN, "shutdown", "--socket", sock],
                               capture_output=True, text=True)
                srv.wait(timeout=10)

    def test_daemon_parse_error_and_idle_timeout_setup(self):
        # parse error path; idle-timeout is set to 30s so we only verify the
        # daemon survives an idle connect-and-drop plus a garbage line.
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td); sock = os.path.join(td, "h.sock")
            srv = subprocess.Popen([BIN, "serve", ll, "--socket", sock],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.wait_for(lambda: os.path.exists(sock), 60)
                # idle client connects and immediately disconnects
                s = socket.socket(socket.AF_UNIX); s.connect(sock); s.close()
                # garbage line -> -32700 with id null
                with socket.socket(socket.AF_UNIX) as g:
                    g.settimeout(10); g.connect(sock)
                    g.sendall(b"this is not json\n")
                    buf = b""
                    while not buf.endswith(b"\n"):
                        chunk = g.recv(65536)
                        if not chunk: break
                        buf += chunk
                resp = json.loads(buf)
                self.assertEqual(resp["error"]["code"], -32700)
                self.assertIsNone(resp["id"])
                # daemon still alive and serving
                r = self.client(sock, "summary", {})
                self.assertIn("functions", r["result"])
            finally:
                self.client(sock, "shutdown", {}); srv.wait(timeout=10)

    def test_schema_self_describing(self):
        j = self.oneshot("schema", {})
        kinds = {k["name"] for k in j["node_kinds"]}
        self.assertLessEqual({"IntraICFGNode", "CallICFGNode", "RetICFGNode",
                              "LoadVFGNode", "StoreVFGNode",
                              "FormalINPHISVFGNode", "ActualOUTPHISVFGNode"}, kinds)
        for k in j["node_kinds"]:
            self.assertTrue(k["description"], f"missing description: {k['name']}")
        edge_names = {e["name"] for e in j["edge_kinds"]}
        self.assertLessEqual({"IntraCFGEdge", "CallCFGEdge", "RetCFGEdge"}, edge_names)
        self.assertEqual(len(j["methods"]), 11)
        for m in j["methods"]:
            self.assertTrue(m["description"]); self.assertIn("params", m)
            if m["implemented"]:
                # every dispatchable method must be fully documented
                self.assertIsInstance(m["params"], dict)
                self.assertTrue(m["returns"], f"missing returns: {m['name']}")
        implemented = {m["name"] for m in j["methods"] if m["implemented"]}
        self.assertLessEqual({"schema", "summary", "functions",
                              "callers", "callees", "cfg", "defuse", "pts",
                              "aliases", "vfpath", "reachable"}, implemented)
        self.assertIn("evidence_record", j)
        self.assertIn("program", j)
        # Drift guard: callers/callees returns docs must describe the real shape.
        method_returns = {m["name"]: m.get("returns", "") for m in j["methods"]}
        for mname in ("callers", "callees"):
            self.assertIn("calls", method_returns[mname],
                          f"{mname} returns doc missing 'calls'")
            self.assertIn("truncated", method_returns[mname],
                          f"{mname} returns doc missing 'truncated'")
        # Drift guard: cfg/defuse/pts/aliases returns docs (Task 4.3 shapes).
        for mname in ("cfg", "defuse", "pts", "aliases"):
            self.assertIn("truncated", method_returns[mname],
                          f"{mname} returns doc missing 'truncated'")
        for mname in ("defuse", "pts", "aliases"):
            self.assertIn("vars", method_returns[mname],
                          f"{mname} returns doc missing 'vars'")
        self.assertIn("points_to", method_returns["pts"])
        self.assertIn("total_nodes", method_returns["cfg"])
        # Drift guard: defuse per-var caps (Task 5.1 carry-over) + vfpath/
        # reachable real shapes (Task 5.1).
        self.assertIn("200", method_returns["defuse"])
        for key in ("paths", "steps", "visited", "truncated"):
            self.assertIn(key, method_returns["vfpath"],
                          f"vfpath returns doc missing '{key}'")
        for key in ("results", "first_path", "truncated"):
            self.assertIn(key, method_returns["reachable"],
                          f"reachable returns doc missing '{key}'")

    @unittest.skipUnless(
        os.path.isdir(os.path.join(HERE, "..", "..", "..", "..", "svf", "lib")),
        "svf/lib sources not present")
    def test_schema_kind_invariant(self):
        out = subprocess.run([sys.executable,
                              os.path.join(HERE, "check_schema_kinds.py")],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_schema_summary_consistency(self):
        # regression: dangling SVFG pointer corrupted svfg_nodes after schema()
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td); sock = os.path.join(td, "h.sock")
            srv = subprocess.Popen([BIN, "serve", ll, "--socket", sock],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.wait_for(lambda: os.path.exists(sock), 60)
                before = self.client(sock, "summary", {})["result"]
                schema = self.client(sock, "schema", {})["result"]
                after = self.client(sock, "summary", {})["result"]
                self.assertEqual(before, after)
                self.assertEqual(schema["program"]["summary"], before)
                self.assertGreater(before["svfg_nodes"], 0)
                self.assertLess(before["svfg_nodes"], 10000)  # demo fixture is tiny
            finally:
                self.client(sock, "shutdown", {}); srv.wait(timeout=10)

    def test_callers_of_fill(self):
        j = self.oneshot("callers", {"func": "fill"})
        self.assertEqual(j["function"], "fill")
        callers = {c["caller"] for c in j["calls"]}
        self.assertIn("use_after_free", callers)
        c = [c for c in j["calls"] if c["caller"] == "use_after_free"][0]
        self.assertTrue(c["callsite"]["loc"]["file"].endswith("demo.c"))
        self.assertTrue(c["direct"])

    def test_callees_of_use_after_free(self):
        j = self.oneshot("callees", {"func": "use_after_free"})
        callees = {c["callee"] for c in j["calls"]}
        self.assertLessEqual({"make_buf", "fill", "free"}, callees)

    def test_indirect_callees_resolved(self):
        j = self.oneshot("callees", {"func": "apply"}, fixture="indirect.c")
        indirect = [c for c in j["calls"] if not c["direct"]]
        self.assertEqual({c["callee"] for c in indirect}, {"dbl", "neg"})

    def test_unknown_function_hint(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "callers", "--params",
                                  json.dumps({"func": "make_buff"}), ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            j = json.loads(out.stdout)
            self.assertIn("make_buf", j["error"]["message"])

    def test_cfg_of_use_after_free(self):
        j = self.oneshot("cfg", {"func": "use_after_free"})
        self.assertEqual(j["function"], "use_after_free")
        self.assertGreater(len(j["nodes"]), 3)
        kinds = {e["kind"] for e in j["edges"]}
        self.assertIn("IntraCFGEdge", kinds)
        files = {n["loc"]["file"] for n in j["nodes"] if n["loc"]["file"]}
        self.assertTrue(all(f.endswith("demo.c") for f in files), files)
        # instruction-level locs arrive via the "fl" key path of evidence::loc
        self.assertTrue(any(n["loc"]["line"] > 0 for n in j["nodes"]))
        self.assertEqual(j["total_nodes"], len(j["nodes"]))
        self.assertEqual(j["total_edges"], len(j["edges"]))
        self.assertFalse(j["truncated"])

    def test_cfg_ir_truncation(self):
        # long_ir calls a 20-arg helper: its CallICFGNode toString() exceeds
        # the ~200-byte ir cap, so some node's ir must end with the ellipsis.
        j = self.oneshot("cfg", {"func": "long_ir"})
        self.assertTrue(any(n["ir"].endswith("…") for n in j["nodes"]),
                        sorted(len(n["ir"].encode()) for n in j["nodes"]))

    def test_pts_of_b_contains_heap_obj(self):
        j = self.oneshot("pts", {"var": {"func": "malloc", "ret": True}})
        self.assertGreaterEqual(len(j["vars"]), 1)
        objs = [o for v in j["vars"] for o in v["points_to"]]
        self.assertTrue(any(o["kind"] == "HeapObjVar" for o in objs), objs)
        heap = [o for o in objs if o["kind"] == "HeapObjVar"][0]
        self.assertEqual(heap["loc"]["line"], 4)  # the malloc in make_buf

    def test_defuse_of_b(self):
        # demo.c line 8: `char* b = make_buf(8);`
        j = self.oneshot("defuse", {"var": {"file": "demo.c", "line": 8,
                                            "name": "b"}})
        self.assertGreaterEqual(len(j["vars"]), 1)
        v = j["vars"][0]
        self.assertTrue(v["defs"], v)
        for s in v["defs"] + v["uses"]:
            self.assertIn("stmt", s); self.assertIn("at", s)
        use_lines = {u["at"]["loc"]["line"] for u in v["uses"]}
        # b is loaded for fill(b) (line 9), free(b) (line 10), b[0] (line 11);
        # require the free callsite line or the return load line.
        self.assertTrue(use_lines & {10, 11}, use_lines)
        # instruction-level locs arrive via the "fl" key path of evidence::loc
        use_files = {u["at"]["loc"]["file"] for u in v["uses"]
                     if u["at"]["loc"]["file"]}
        self.assertTrue(use_files, v["uses"])
        self.assertTrue(all(f.endswith("demo.c") for f in use_files), use_files)

    def test_aliases_of_malloc_ret(self):
        # Andersen MAY-alias, v0 scope: candidates are ValVars of the var's
        # OWN function only. The malloc result lives in make_buf, where it
        # flows to make_buf's unique return-value var — so at least one alias
        # (sharing the line-4 heap object) must be reported. b/p in other
        # functions are deliberately out of scope in v0.
        j = self.oneshot("aliases", {"var": {"func": "malloc", "ret": True}})
        self.assertGreaterEqual(len(j["vars"]), 1)
        v = j["vars"][0]
        self.assertTrue(v["aliases"], j)
        # the var itself is excluded from its alias list
        self.assertTrue(all(a["id"] != v["var"]["id"] for a in v["aliases"]))

    def test_var_resolution_error_hint(self):
        # nonexistent line -> error listing the accepted anchor forms
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "defuse", "--params",
                                  json.dumps({"var": {"file": "demo.c",
                                                      "line": 999}}), ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            msg = json.loads(out.stdout)["error"]["message"]
            self.assertIn("file", msg)
            self.assertIn("999", msg)
            self.assertIn("nearest", msg)  # nearby defining lines are listed

    def test_vfpath_malloc_to_use(self):
        j = self.oneshot("vfpath", {
            "source": {"func": "malloc", "ret": True},
            "sink":   {"file": "demo.c", "line": 11},
            "k": 3})
        self.assertGreaterEqual(len(j["paths"]), 1)
        p = j["paths"][0]
        files = {s["node"]["loc"]["file"].split("/")[-1]
                 for s in p["steps"] if s["node"]["loc"]["file"]}
        self.assertEqual(files, {"demo.c"})
        edge_kinds = [s["edge"] for s in p["steps"][1:]]
        self.assertTrue(any("Ret" in k or "Call" in k for k in edge_kinds),
                        edge_kinds)  # crosses the make_buf boundary
        self.assertIn("truncated", j)

    def test_reachable_batch(self):
        # demo.c line 22 is `return a0 + a19;` inside long_ir_helper — it
        # defines values, but they never flow from malloc.
        j = self.oneshot("reachable", {
            "source": {"func": "malloc", "ret": True},
            "sinks": [{"file": "demo.c", "line": 11},
                      {"file": "demo.c", "line": 22}]})
        self.assertEqual(len(j["results"]), 2)
        self.assertTrue(j["results"][0]["reachable"])
        r0 = j["results"][0]
        self.assertGreaterEqual(len(r0["first_path"]["steps"]), 2)

    def test_vfpath_unreachable(self):
        # long_ir_helper's locals never flow from malloc
        j = self.oneshot("vfpath", {
            "source": {"func": "malloc", "ret": True},
            "sink": {"file": "demo.c", "line": 22}, "k": 1})
        self.assertEqual(j["paths"], [])

    def test_reachable_sink_cap(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            params = {"source": {"func": "malloc", "ret": True},
                      "sinks": [{"file": "demo.c", "line": 11}] * 21}
            out = subprocess.run([BIN, "--oneshot", "reachable", "--params",
                                  json.dumps(params), ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            msg = json.loads(out.stdout)["error"]["message"]
            self.assertIn("20", msg)  # the cap is named in the error

    def test_anchor_name_filter_miss_hint(self):
        # line 8 defines vars, but none named 'zzz': the error must blame the
        # name filter (and mention -fno-discard-value-names), not the line.
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "defuse", "--params",
                                  json.dumps({"var": {"file": "demo.c",
                                                      "line": 8,
                                                      "name": "zzz"}}), ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            msg = json.loads(out.stdout)["error"]["message"]
            self.assertIn("name filter", msg)
            self.assertIn("-fno-discard-value-names", msg)

    def test_func_name_anchor_unsupported(self):
        # {func, name} is not an accepted form; the error must say so
        # explicitly instead of the generic "needs ret or arg" message.
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "defuse", "--params",
                                  json.dumps({"var": {"func": "fill",
                                                      "name": "p"}}), ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            msg = json.loads(out.stdout)["error"]["message"]
            self.assertIn("{func, name}", msg)
            self.assertIn("unsupported", msg)

    def test_duplicate_function_names_merged(self):
        j = self.oneshot("callers", {"func": "helper"},
                         fixture=["dup_a.c", "dup_b.c"])
        self.assertEqual(j["matched_functions"], 2)
        callers = {c["caller"] for c in j["calls"]}
        self.assertEqual(callers, {"entry_a", "entry_b"})

if __name__ == "__main__":
    unittest.main()
