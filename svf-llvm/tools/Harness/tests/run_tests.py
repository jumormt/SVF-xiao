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
        with tempfile.TemporaryDirectory() as td:
            ll = build_fixture(td, fixture)
            out = subprocess.run([BIN, "--oneshot", method, "--params",
                                  json.dumps(params), ll],
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

if __name__ == "__main__":
    unittest.main()
