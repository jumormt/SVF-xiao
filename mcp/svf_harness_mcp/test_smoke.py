#!/usr/bin/env python3
"""Smoke tests for the svf-harness MCP wrapper.

Run with a python >= 3.10 that has the `mcp` SDK installed, e.g.:

    SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness \
        /home/xiao/program/py311-mcp/bin/python mcp/svf_harness_mcp/test_smoke.py -v

Uses the SDK's in-memory transport (mcp.shared.memory, SDK 1.27.2) — no stdio
subprocess needed; the daemon, however, is spawned for real by load_program.
"""
import asyncio
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    from mcp.shared.memory import create_connected_server_and_client_session
except ImportError as e:  # pragma: no cover
    sys.exit(f"error: the `mcp` SDK is required to run this test ({e}); "
             "run with an interpreter that has `pip install mcp`")

import server  # noqa: E402  (the FastMCP app under test)

FIXTURE = os.path.abspath(os.path.join(
    HERE, "..", "..", "svf-llvm", "tools", "Harness", "tests", "fixtures",
    "demo.c"))
CLANG = os.environ.get("CLANG", shutil.which("clang"))

LIFECYCLE_TOOLS = {"load_program", "unload_program"}
EXPECTED_QUERY_TOOLS = {
    "schema", "summary", "functions", "callers", "callees", "cfg",
    "defuse", "pts", "aliases", "cfl_pts", "cfl_aliases",
    "dda_pts", "dda_aliases",
    "saber_leaks", "saber_double_frees", "saber_file_leaks",
    "mta_summary", "mta_mhp",
    "vfpath", "reachable",
    "graphs", "graph_nodes", "graph_edges", "node", "neighbors",
    "analysis_config",
}
EXPECTED_TOOLS = LIFECYCLE_TOOLS | EXPECTED_QUERY_TOOLS


def build_fixture(tmpdir):
    out = os.path.join(tmpdir, "demo.ll")
    subprocess.check_call([CLANG, "-S", "-emit-llvm", "-g", "-O0",
                           "-fno-discard-value-names", "-o", out, FIXTURE])
    return out


class McpSmokeTest(unittest.TestCase):
    """Test names are prefixed a_/b_/c_ to pin unittest's alphabetical order:
    the no-program error check must run before anything loads a daemon."""

    def run_session(self, coro_fn):
        async def main():
            async with create_connected_server_and_client_session(
                    server.app) as session:
                return await coro_fn(session)
        return asyncio.run(main())

    def call(self, session, tool, args):
        """call_tool + unwrap: tools return dict[str, Any] -> structuredContent
        is the dict itself (RootModel, not {'result': ...}-wrapped)."""
        async def go():
            res = await session.call_tool(tool, args)
            self.assertFalse(res.isError, res.content)
            self.assertIsInstance(res.structuredContent, dict)
            return res.structuredContent
        return go()

    def test_a_query_before_load_is_error_not_exception(self):
        async def scenario(session):
            # Ensure any previous daemon state is cleared first so the test is
            # order-independent.
            await self.call(session, "unload_program", {})
            out = await self.call(session, "summary", {})
            self.assertIn("error", out)
            self.assertIn("load_program", out["error"])
        self.run_session(scenario)

    def test_a2_load_nonexistent_path_returns_error(self):
        """load_program with a nonexistent path must return an error dict, not raise."""
        async def scenario(session):
            out = await self.call(session, "load_program",
                                  {"bitcode_paths": ["/nonexistent/path/to/file.ll"]})
            self.assertIn("error", out)
        self.run_session(scenario)

    def test_b_list_tools(self):
        async def scenario(session):
            tools = (await session.list_tools()).tools
            self.assertEqual({t.name for t in tools}, EXPECTED_TOOLS)
            self.assertEqual(len(tools), 28)
            for t in tools:
                self.assertTrue(t.description, f"missing description: {t.name}")
        self.run_session(scenario)

    @unittest.skipUnless(CLANG, "clang not found; set CLANG=/path/to/clang")
    def test_c_load_query_unload(self):
        async def scenario(session):
            with tempfile.TemporaryDirectory() as td:
                ll = build_fixture(td)
                try:
                    loaded = await self.call(session, "load_program",
                                             {"bitcode_paths": [ll],
                                              "analysis_config": {
                                                  "svfg": {"mode": "ptr-only"}}})
                    self.assertNotIn("error", loaded)
                    self.assertGreaterEqual(loaded["functions"], 4)
                    self.assertEqual(loaded["modules"], [ll])
                    self.assertTrue(os.path.exists(loaded["socket_path"]))
                    self.assertEqual(loaded["analysis_config"]["svfg"]["mode"],
                                     "ptr-only")

                    summary = await self.call(session, "summary", {})
                    for key in ("functions", "icfg_nodes", "svfg_nodes"):
                        self.assertEqual(summary[key], loaded[key], key)

                    schema = await self.call(session, "schema", {})
                    schema_methods = {m["name"] for m in schema["methods"]
                                      if m.get("implemented")}
                    tools = (await session.list_tools()).tools
                    query_tools = {t.name for t in tools} - LIFECYCLE_TOOLS
                    self.assertEqual(query_tools, schema_methods)

                    # the money shot: malloc return -> use at demo.c:11
                    vf = await self.call(session, "vfpath", {"params": {
                        "source": {"func": "malloc", "ret": True},
                        "sink": {"file": "demo.c", "line": 11}, "k": 1}})
                    self.assertNotIn("error", vf)
                    self.assertGreaterEqual(len(vf["paths"]), 1)
                    self.assertGreaterEqual(len(vf["paths"][0]["steps"]), 2)
                finally:
                    out = await self.call(session, "unload_program", {})
                    self.assertTrue(out["ok"])
            # after unload, queries error again (and the daemon is gone)
            out = await self.call(session, "functions",
                                  {"params": {"pattern": "main"}})
            self.assertIn("error", out)
        self.run_session(scenario)


if __name__ == "__main__":
    unittest.main()
