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

if __name__ == "__main__":
    unittest.main()
