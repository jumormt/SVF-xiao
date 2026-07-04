#!/usr/bin/env python3
import json, os, shutil, socket, subprocess, sys, tempfile, time, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
TEST_SUITE_BC_ROOT = os.path.join(PROJECT_ROOT, "Test-Suite", "test_cases_bc")
TEST_SUITE_CRUX_BC = os.path.join(TEST_SUITE_BC_ROOT, "crux-bc", "bc.bc")
TEST_SUITE_CPP_ARRAY_BC = os.path.join(
    TEST_SUITE_BC_ROOT, "basic_cpp_tests", "array-3.cpp.bc")
TEST_SUITE_MEM_LEAK_BC = os.path.join(
    TEST_SUITE_BC_ROOT, "mem_leak", "malloc0.c.bc")
TEST_SUITE_DOUBLE_FREE_BC = os.path.join(
    TEST_SUITE_BC_ROOT, "double_free", "df0.c.bc")
TEST_SUITE_MTA_SIMPLE_BC = os.path.join(
    TEST_SUITE_BC_ROOT, "mta", "succ_cxt_simple_2.c.bc")
BIN = os.environ.get("SVF_HARNESS_BIN", "svf-harness")
if not os.path.isfile(BIN) and shutil.which(BIN) is None:
    sys.exit(f"set SVF_HARNESS_BIN=/path/to/Release-build/bin/svf-harness (got: {BIN!r})")
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

    def test_cli_help_methods_match_schema(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            help_out = subprocess.run([BIN, "--help"],
                                      capture_output=True, text=True)
            self.assertEqual(help_out.returncode, 0)
            methods_lines = [line for line in help_out.stdout.splitlines()
                             if line.startswith("Methods:")]
            self.assertEqual(len(methods_lines), 1, help_out.stdout)
            help_methods = methods_lines[0].split(":", 1)[1].split()

            schema_out = subprocess.run([BIN, "--oneshot", "schema", ll],
                                        capture_output=True, text=True)
            self.assertEqual(schema_out.returncode, 0,
                             schema_out.stderr + schema_out.stdout)
            schema = json.loads(schema_out.stdout)
            schema_methods = [m["name"] for m in schema["methods"]
                              if m.get("implemented")]
            self.assertEqual(help_methods, schema_methods)

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

    def oneshot(self, method, params, fixture="demo.c", analysis_config=None):
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
            cmd = [BIN, "--oneshot", method, "--params", json.dumps(params)]
            if analysis_config is not None:
                cmd += ["--analysis-config", json.dumps(analysis_config)]
            cmd += lls
            out = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, f"stderr={out.stderr} stdout={out.stdout}")
            return json.loads(out.stdout)

    def oneshot_bitcode(self, method, params, bitcode_paths,
                        analysis_config=None):
        """Run a one-shot query against existing LLVM bitcode/IR files."""
        cmd = [BIN, "--oneshot", method, "--params", json.dumps(params)]
        if analysis_config is not None:
            cmd += ["--analysis-config", json.dumps(analysis_config)]
        cmd += bitcode_paths
        out = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(out.returncode, 0,
                         f"stderr={out.stderr} stdout={out.stdout}")
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
        self.assertEqual(len(j["methods"]), 26)
        for m in j["methods"]:
            self.assertTrue(m["description"]); self.assertIn("params", m)
            if m["implemented"]:
                # every dispatchable method must be fully documented
                self.assertIsInstance(m["params"], dict)
                self.assertTrue(m["returns"], f"missing returns: {m['name']}")
        implemented = {m["name"] for m in j["methods"] if m["implemented"]}
        self.assertLessEqual({"schema", "summary", "functions",
                              "callers", "callees", "cfg", "defuse", "pts",
                              "aliases", "cfl_pts", "cfl_aliases",
                              "dda_pts", "dda_aliases",
                              "saber_leaks", "saber_double_frees",
                              "saber_file_leaks",
                              "mta_summary", "mta_mhp",
                              "vfpath", "reachable",
                              "graphs", "graph_nodes", "graph_edges",
                              "node", "neighbors", "analysis_config"},
                             implemented)
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
        self.assertIn("points_to", method_returns["cfl_pts"])
        self.assertIn("aliases", method_returns["cfl_aliases"])
        self.assertIn("points_to", method_returns["dda_pts"])
        self.assertIn("aliases", method_returns["dda_aliases"])
        for mname in ("saber_leaks", "saber_double_frees",
                      "saber_file_leaks"):
            self.assertIn("bugs", method_returns[mname])
            self.assertIn("checker", method_returns[mname])
        self.assertIn("fork_sites", method_returns["mta_summary"])
        self.assertIn("threads", method_returns["mta_summary"])
        self.assertIn("may_happen_in_parallel", method_returns["mta_mhp"])
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

    def test_schema_graph_query_methods(self):
        j = self.oneshot("schema", {})
        methods = j["methods"]
        method_names = [m["name"] for m in methods if m["implemented"]]
        self.assertEqual(method_names[-6:-1],
                         ["graphs", "graph_nodes", "graph_edges",
                          "node", "neighbors"])
        by_name = {m["name"]: m for m in methods}
        for name in ("graphs", "graph_nodes", "graph_edges", "node",
                     "neighbors"):
            m = by_name[name]
            self.assertTrue(m["description"], name)
            self.assertIsInstance(m["params"], dict)
            self.assertTrue(m["returns"], name)

    def test_schema_analysis_config_method(self):
        j = self.oneshot("schema", {})
        methods = [m["name"] for m in j["methods"] if m["implemented"]]
        self.assertEqual(methods[-1], "analysis_config")
        m = {m["name"]: m for m in j["methods"]}["analysis_config"]
        self.assertTrue(m["description"])
        self.assertIsInstance(m["params"], dict)
        self.assertTrue(m["returns"])
        self.assertIn("analysis_config", j["program"])

    def test_analysis_config_default(self):
        j = self.oneshot("analysis_config", {})
        self.assertEqual(j["pointer_analysis"]["active"], "andersen-wave-diff")
        self.assertEqual(j["svfg"]["mode"], "full")
        self.assertIn("ptr-only", j["svfg"]["supported_modes"])
        planned = {s["name"]: s["status"] for s in j["surfaces"]}
        self.assertEqual(planned["cfl"], "supported")
        self.assertEqual(planned["dda"], "supported")
        self.assertEqual(planned["saber"], "supported")
        self.assertEqual(planned["mta"], "supported")
        self.assertEqual(planned["ae"], "planned")

    def test_schema_cfl_methods(self):
        j = self.oneshot("schema", {})
        methods = {m["name"]: m for m in j["methods"]}
        for name in ("cfl_pts", "cfl_aliases"):
            self.assertIn(name, methods)
            self.assertTrue(methods[name]["implemented"])
            self.assertIn("var", methods[name]["params"])
            self.assertIn("cfl", methods[name]["description"].lower())

    def test_schema_dda_methods(self):
        j = self.oneshot("schema", {})
        methods = {m["name"]: m for m in j["methods"]}
        for name in ("dda_pts", "dda_aliases"):
            self.assertIn(name, methods)
            self.assertTrue(methods[name]["implemented"])
            self.assertIn("var", methods[name]["params"])
            self.assertIn("dda", methods[name]["description"].lower())

    def test_schema_saber_methods(self):
        j = self.oneshot("schema", {})
        methods = {m["name"]: m for m in j["methods"]}
        for name in ("saber_leaks", "saber_double_frees",
                     "saber_file_leaks"):
            self.assertIn(name, methods)
            self.assertTrue(methods[name]["implemented"])
            self.assertIsInstance(methods[name]["params"], dict)
            self.assertIn("saber", methods[name]["description"].lower())

    def test_schema_mta_methods(self):
        j = self.oneshot("schema", {})
        methods = {m["name"]: m for m in j["methods"]}
        for name in ("mta_summary", "mta_mhp"):
            self.assertIn(name, methods)
            self.assertTrue(methods[name]["implemented"])
            self.assertIsInstance(methods[name]["params"], dict)
            self.assertIn("mta", methods[name]["description"].lower())

    def test_cfl_pts_of_malloc_ret_contains_heap_obj(self):
        j = self.oneshot("cfl_pts", {"var": {"func": "malloc", "ret": True}})
        self.assertEqual(j["analysis"], "cfl-alias")
        self.assertGreaterEqual(len(j["vars"]), 1)
        objs = [o for v in j["vars"] for o in v["points_to"]]
        self.assertTrue(any(o["kind"] == "HeapObjVar" for o in objs), objs)
        heap = [o for o in objs if o["kind"] == "HeapObjVar"][0]
        self.assertEqual(heap["loc"]["line"], 4)

    def test_cfl_aliases_of_malloc_ret(self):
        j = self.oneshot("cfl_aliases",
                         {"var": {"func": "malloc", "ret": True}})
        self.assertEqual(j["analysis"], "cfl-alias")
        self.assertGreaterEqual(len(j["vars"]), 1)
        self.assertTrue(j["vars"][0]["aliases"], j)
        var_id = j["vars"][0]["var"]["id"]
        self.assertTrue(all(a["id"] != var_id for a in j["vars"][0]["aliases"]))

    def test_dda_pts_of_malloc_ret_contains_heap_obj(self):
        j = self.oneshot("dda_pts", {"var": {"func": "malloc", "ret": True}})
        self.assertEqual(j["analysis"], "flowdda")
        self.assertGreaterEqual(len(j["vars"]), 1)
        objs = [o for v in j["vars"] for o in v["points_to"]]
        self.assertTrue(any(o["kind"] == "HeapObjVar" for o in objs), objs)
        heap = [o for o in objs if o["kind"] == "HeapObjVar"][0]
        self.assertEqual(heap["loc"]["line"], 4)

    def test_dda_aliases_of_malloc_ret(self):
        j = self.oneshot("dda_aliases",
                         {"var": {"func": "malloc", "ret": True}})
        self.assertEqual(j["analysis"], "flowdda")
        self.assertGreaterEqual(len(j["vars"]), 1)
        self.assertTrue(j["vars"][0]["aliases"], j)
        var_id = j["vars"][0]["var"]["id"]
        self.assertTrue(all(a["id"] != var_id for a in j["vars"][0]["aliases"]))

    def test_svfg_ptr_only_config_reported(self):
        cfg = {"svfg": {"mode": "ptr-only"}}
        j = self.oneshot("analysis_config", {}, analysis_config=cfg)
        self.assertEqual(j["svfg"]["mode"], "ptr-only")
        graphs = self.oneshot("graphs", {}, analysis_config=cfg)
        by_name = {g["name"]: g for g in graphs["graphs"]}
        self.assertGreater(by_name["svfg"]["nodes"], 0)
        self.assertGreater(by_name["svfg"]["edges"], 0)

    def test_invalid_analysis_config_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td)
            out = subprocess.run([BIN, "--oneshot", "analysis_config",
                                  "--analysis-config",
                                  json.dumps({"svfg": {"mode": "wat"}}),
                                  ll],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 1)
            j = json.loads(out.stdout)
            self.assertIn("unknown svfg.mode", j["error"]["message"])

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

    def test_graphs_inventory(self):
        j = self.oneshot("graphs", {})
        by_name = {g["name"]: g for g in j["graphs"]}
        self.assertLessEqual({"icfg", "svfg", "svfir", "callgraph"},
                             set(by_name))
        self.assertGreater(by_name["icfg"]["nodes"], 0)
        self.assertGreater(by_name["svfg"]["edges"], 0)
        self.assertGreater(by_name["svfir"]["nodes"], 0)
        self.assertGreater(by_name["callgraph"]["edges"], 0)

    def test_graph_nodes_svfg_load_filter(self):
        j = self.oneshot("graph_nodes", {"graph": "svfg",
                                          "kind": "LoadVFGNode",
                                          "func": "use_after_free",
                                          "limit": 10})
        self.assertEqual(j["graph"], "svfg")
        self.assertGreaterEqual(j["total"], 1)
        self.assertTrue(all(n["kind"] == "LoadVFGNode" for n in j["nodes"]))
        self.assertTrue(any(n["loc"]["line"] == 11 for n in j["nodes"]), j)

    def test_graph_edges_icfg_kind_filter(self):
        j = self.oneshot("graph_edges", {"graph": "icfg",
                                          "kind": "IntraCFGEdge",
                                          "limit": 5})
        self.assertEqual(j["graph"], "icfg")
        self.assertGreater(j["total"], 0)
        self.assertTrue(j["truncated"])
        self.assertTrue(all(e["kind"] == "IntraCFGEdge" for e in j["edges"]))
        for e in j["edges"]:
            self.assertIn("src", e); self.assertIn("dst", e)

    def test_graph_node_and_neighbors_roundtrip(self):
        loads = self.oneshot("graph_nodes", {"graph": "svfg",
                                             "kind": "LoadVFGNode",
                                             "func": "use_after_free",
                                             "limit": 10})
        target = [n for n in loads["nodes"] if n["loc"]["line"] == 11][0]
        one = self.oneshot("node", {"graph": "svfg", "id": target["id"]})
        self.assertEqual(one["node"], target)
        nbrs = self.oneshot("neighbors", {"graph": "svfg",
                                           "id": target["id"]})
        self.assertEqual(nbrs["node"], target)
        self.assertTrue(nbrs.get("in_edges") or nbrs.get("out_edges"), nbrs)

    @unittest.skipUnless(os.path.isfile(TEST_SUITE_CRUX_BC),
                         "Test-Suite crux-bc bitcode not present")
    def test_testsuite_crux_bc_summary_and_graphs(self):
        summary = self.oneshot_bitcode("summary", {}, [TEST_SUITE_CRUX_BC])
        self.assertGreater(summary["functions"], 100)
        self.assertGreater(summary["svfg_nodes"], 1000)

        graphs = self.oneshot_bitcode("graphs", {}, [TEST_SUITE_CRUX_BC])
        by_name = {g["name"]: g for g in graphs["graphs"]}
        self.assertLessEqual({"icfg", "svfg", "svfir", "callgraph"},
                             set(by_name))
        self.assertGreater(by_name["svfg"]["nodes"], 1000)
        self.assertGreater(by_name["svfg"]["edges"], 1000)

        funcs = self.oneshot_bitcode("functions", {"pattern": "free|malloc"},
                                     [TEST_SUITE_CRUX_BC])
        self.assertGreater(funcs["total"], 0)

        nodes = self.oneshot_bitcode("graph_nodes",
                                     {"graph": "svfg", "limit": 25},
                                     [TEST_SUITE_CRUX_BC])
        self.assertEqual(nodes["graph"], "svfg")
        self.assertEqual(len(nodes["nodes"]), 25)
        self.assertTrue(nodes["truncated"])

        ptr_graphs = self.oneshot_bitcode(
            "graphs", {}, [TEST_SUITE_CRUX_BC],
            analysis_config={"svfg": {"mode": "ptr-only"}})
        ptr_svfg = {g["name"]: g for g in ptr_graphs["graphs"]}["svfg"]
        self.assertGreater(ptr_svfg["nodes"], 0)
        self.assertLess(ptr_svfg["nodes"], by_name["svfg"]["nodes"])

    @unittest.skipUnless(os.path.isfile(TEST_SUITE_CPP_ARRAY_BC),
                         "Test-Suite C++ array bitcode not present")
    def test_testsuite_cpp_callgraph_smoke(self):
        summary = self.oneshot_bitcode("summary", {},
                                       [TEST_SUITE_CPP_ARRAY_BC])
        self.assertGreater(summary["functions"], 0)
        self.assertGreater(summary["icfg_nodes"], 0)

        funcs = self.oneshot_bitcode("functions", {"pattern": "main"},
                                     [TEST_SUITE_CPP_ARRAY_BC])
        self.assertTrue(any(f["name"] == "main" for f in funcs["functions"]),
                        funcs)

        nodes = self.oneshot_bitcode("graph_nodes",
                                     {"graph": "callgraph", "limit": 10},
                                     [TEST_SUITE_CPP_ARRAY_BC])
        self.assertEqual(nodes["graph"], "callgraph")
        self.assertGreater(nodes["total"], 0)

        edges = self.oneshot_bitcode("graph_edges",
                                     {"graph": "callgraph", "limit": 20},
                                     [TEST_SUITE_CPP_ARRAY_BC])
        self.assertEqual(edges["graph"], "callgraph")
        self.assertIn("edges", edges)
        self.assertGreater(edges["total"], 0)

    @unittest.skipUnless(os.path.isfile(TEST_SUITE_CPP_ARRAY_BC),
                         "Test-Suite C++ array bitcode not present")
    def test_testsuite_cpp_cfl_operator_new_smoke(self):
        j = self.oneshot_bitcode("cfl_pts",
                                 {"var": {"func": "_Znwm", "ret": True}},
                                 [TEST_SUITE_CPP_ARRAY_BC])
        self.assertEqual(j["analysis"], "cfl-alias")
        self.assertGreater(j["total"], 0)
        objs = [o for v in j["vars"] for o in v["points_to"]]
        self.assertTrue(any(o["kind"] == "HeapObjVar" for o in objs), objs)

    @unittest.skipUnless(os.path.isfile(TEST_SUITE_CPP_ARRAY_BC),
                         "Test-Suite C++ array bitcode not present")
    def test_testsuite_cpp_dda_operator_new_smoke(self):
        j = self.oneshot_bitcode("dda_pts",
                                 {"var": {"func": "_Znwm", "ret": True}},
                                 [TEST_SUITE_CPP_ARRAY_BC])
        self.assertEqual(j["analysis"], "flowdda")
        self.assertGreater(j["total"], 0)
        objs = [o for v in j["vars"] for o in v["points_to"]]
        self.assertTrue(any(o["kind"] == "HeapObjVar" for o in objs), objs)

    @unittest.skipUnless(os.path.isfile(TEST_SUITE_MEM_LEAK_BC),
                         "Test-Suite mem_leak malloc0 bitcode not present")
    def test_testsuite_saber_leak_smoke(self):
        j = self.oneshot_bitcode("saber_leaks", {}, [TEST_SUITE_MEM_LEAK_BC])
        self.assertEqual(j["checker"], "leak")
        self.assertGreaterEqual(j["total"], 2, j)
        self.assertGreater(j["sources"], 0)
        bug_types = {b["type"] for b in j["bugs"]}
        self.assertIn("Never Free", bug_types)
        locs = [b["loc"] for b in j["bugs"]]
        self.assertTrue(any(loc["file"].endswith("malloc0.c") and
                            loc["line"] in (12, 13) for loc in locs), locs)

    @unittest.skipUnless(os.path.isfile(TEST_SUITE_DOUBLE_FREE_BC),
                         "Test-Suite double_free df0 bitcode not present")
    def test_testsuite_saber_double_free_smoke(self):
        j = self.oneshot_bitcode("saber_double_frees", {},
                                 [TEST_SUITE_DOUBLE_FREE_BC])
        self.assertEqual(j["checker"], "double-free")
        self.assertGreaterEqual(j["total"], 1, j)
        self.assertGreater(j["sources"], 0)
        bug_types = {b["type"] for b in j["bugs"]}
        self.assertIn("Double Free", bug_types)

    def test_mta_summary_fixture(self):
        j = self.oneshot("mta_summary", {}, fixture="thread_mhp.c")
        self.assertEqual(j["analysis"], "mta")
        self.assertGreaterEqual(j["threads"], 2, j)
        self.assertGreaterEqual(j["fork_sites"], 1, j)
        self.assertGreaterEqual(j["join_sites"], 1, j)
        self.assertTrue(j["forks"], j)
        self.assertTrue(j["joins"], j)
        self.assertTrue(j["forks"][0]["callsite"]["loc"]["file"].endswith(
            "thread_mhp.c"))

    def test_mta_mhp_fixture(self):
        j = self.oneshot("mta_mhp",
                         {"left": {"file": "thread_mhp.c", "line": 6},
                          "right": {"file": "thread_mhp.c", "line": 12}},
                         fixture="thread_mhp.c")
        self.assertEqual(j["analysis"], "mta")
        self.assertGreaterEqual(j["left_matches"], 1, j)
        self.assertGreaterEqual(j["right_matches"], 1, j)
        self.assertTrue(j["may_happen_in_parallel"], j)
        self.assertTrue(j["witnesses"], j)

    @unittest.skipUnless(os.path.isfile(TEST_SUITE_MTA_SIMPLE_BC),
                         "Test-Suite MTA simple bitcode not present")
    def test_testsuite_mta_summary_smoke(self):
        j = self.oneshot_bitcode("mta_summary", {},
                                 [TEST_SUITE_MTA_SIMPLE_BC])
        self.assertEqual(j["analysis"], "mta")
        self.assertGreaterEqual(j["threads"], 2, j)
        self.assertGreaterEqual(j["fork_sites"], 1, j)
        self.assertGreaterEqual(j["join_sites"], 1, j)

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

    def test_reachable_tolerates_bad_sink(self):
        # A bad sink (nonexistent line 999) must yield a per-sink error row,
        # not abort the whole call — the good sink (line 11) must still report
        # reachable=True.
        j = self.oneshot("reachable", {
            "source": {"func": "malloc", "ret": True},
            "sinks": [{"file": "demo.c", "line": 11},
                      {"file": "demo.c", "line": 999}]})
        self.assertEqual(len(j["results"]), 2)
        self.assertTrue(j["results"][0]["reachable"])
        self.assertFalse(j["results"][1]["reachable"])
        self.assertIn("error", j["results"][1])

    def test_vfpath_step_elision(self):
        # chain.c: alloc() returns malloc; hop0..hop9 each do a local
        # store+load, threading the pointer; main dereferences it on line 25.
        # The path length exceeds 10 steps (each hop adds store/load/phi/ret
        # nodes), so max_steps=10 exercises middle elision.
        j = self.oneshot("vfpath", {
            "source": {"func": "malloc", "ret": True},
            "sink":   {"file": "chain.c", "line": 25},
            "k": 1, "max_steps": 10}, fixture="chain.c")
        self.assertGreaterEqual(len(j["paths"]), 1)
        p = j["paths"][0]
        self.assertTrue(p.get("steps_truncated"), p)
        markers = [s for s in p["steps"] if "elided_steps" in s]
        self.assertEqual(len(markers), 1)
        self.assertGreater(markers[0]["elided_steps"], 0)

    MCP_PYTHON = os.environ.get("MCP_PYTHON",
                                "/home/xiao/program/py311-mcp/bin/python")

    @unittest.skipUnless(os.path.isfile(MCP_PYTHON),
                         "MCP python (>=3.10 with `mcp`) not present; "
                         "set MCP_PYTHON to enable")
    def test_mcp_smoke(self):
        smoke = os.path.abspath(os.path.join(
            HERE, "..", "..", "..", "..", "mcp", "svf_harness_mcp",
            "test_smoke.py"))
        env = dict(os.environ, SVF_HARNESS_BIN=os.path.abspath(BIN)
                   if os.path.isfile(BIN) else BIN)
        out = subprocess.run([self.MCP_PYTHON, smoke], capture_output=True,
                             text=True, env=env, timeout=300)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_duplicate_function_names_merged(self):
        j = self.oneshot("callers", {"func": "helper"},
                         fixture=["dup_a.c", "dup_b.c"])
        self.assertEqual(j["matched_functions"], 2)
        callers = {c["caller"] for c in j["calls"]}
        self.assertEqual(callers, {"entry_a", "entry_b"})

if __name__ == "__main__":
    unittest.main()
