"""svf-harness MCP server: thin stdio adapter over the svf-harness daemon socket.

Setup for Codex (adjust the python path to an interpreter with `mcp`
installed, e.g. /home/xiao/program/py311-mcp/bin/python):

    codex mcp add svf \
        --env SVF_HARNESS_BIN=/path/to/Release-build/bin/svf-harness \
        -- /path/to/python /path/to/SVF-xiao/mcp/svf_harness_mcp/server.py

This checkout also has a project-scoped `.codex/config.toml` for Codex.

Setup for Claude Code:

    claude mcp add svf \
        --env SVF_HARNESS_BIN=/path/to/Release-build/bin/svf-harness \
        -- /path/to/python /path/to/SVF-xiao/mcp/svf_harness_mcp/server.py

No daemon is spawned at startup: call the `load_program` tool with bitcode
paths first; it spawns `svf-harness serve` and the 28 query tools then forward
JSON-RPC over its Unix socket. Call the `schema` tool for the authoritative
self-describing contract of every method.
"""

import ctypes
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from typing import Any

import anyio
import anyio.to_thread
from mcp.server.fastmcp import FastMCP

app = FastMCP("svf-harness")

# Module-level daemon state. Exactly one daemon at a time; load_program
# replaces it.
_state: dict[str, Any] = {
    "socket": None,    # socket path (str) once a daemon is up
    "proc": None,      # subprocess.Popen handle
    "modules": [],     # bitcode paths the daemon was started with
    "tmpdir": None,    # holds the socket + daemon.log
    "next_id": 0,      # JSON-RPC id counter
}

# Serialise load_program/unload_program so two concurrent calls cannot race
# (both "succeed" with two daemons, one getting its tmpdir deleted/orphaned).
_lifecycle_lock: anyio.Lock | None = None  # created lazily after event-loop start

_LOAD_TIMEOUT_S = 600   # analysis state builds once at serve; big programs are slow
_QUERY_TIMEOUT_S = 30   # per-request socket timeout for daemon queries
_POLL_S = 0.2


def _get_lifecycle_lock() -> anyio.Lock:
    """Return (creating if needed) the module-level lifecycle lock."""
    global _lifecycle_lock
    if _lifecycle_lock is None:
        _lifecycle_lock = anyio.Lock()
    return _lifecycle_lock


# ---------------------------------------------------------------------------
# pdeathsig helper (Linux-only): makes the daemon receive SIGTERM when the
# wrapper python process dies (even via SIGKILL), preventing orphaned daemons.
# ---------------------------------------------------------------------------
def _set_pdeathsig() -> None:
    """preexec_fn: ask the kernel to deliver SIGTERM to the child when we die."""
    PR_SET_PDEATHSIG = 1
    ctypes.CDLL("libc.so.6", use_errno=True).prctl(
        PR_SET_PDEATHSIG, signal.SIGTERM
    )


def _rpc(method: str, params: dict[str, Any], timeout: float = _QUERY_TIMEOUT_S) -> dict[str, Any]:
    """One JSON-RPC round trip over the daemon socket.

    Never raises for daemon/protocol failures: every failure becomes a
    structured {"error": ...} return value (LLM-friendly).
    """
    if _state["socket"] is None:
        return {"error": "no program loaded; call load_program first"}
    _state["next_id"] += 1
    req = {"jsonrpc": "2.0", "id": _state["next_id"], "method": method,
           "params": params or {}}
    try:
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(timeout)
            s.connect(_state["socket"])
            s.sendall((json.dumps(req) + "\n").encode())
            buf = b""
            while not buf.endswith(b"\n"):
                chunk = s.recv(65536)
                if not chunk:
                    break
                buf += chunk
    except OSError as e:
        return {"error": f"daemon unreachable ({e}); call load_program again"}
    try:
        resp = json.loads(buf)
    except ValueError as e:
        return {"error": f"daemon sent malformed JSON ({e}); call load_program again"}
    if "error" in resp:
        err = resp["error"]
        out = {"code": err.get("code"), "message": err.get("message")}
        hint = (err.get("data") or {}).get("hint")
        if hint:
            out["hint"] = hint
        return {"error": out}
    return resp.get("result")


def _shutdown_daemon() -> None:
    """Stop the current daemon (graceful shutdown, SIGTERM fallback) + cleanup."""
    proc = _state["proc"]
    if proc is not None and proc.poll() is None:
        try:
            _rpc("shutdown", {})
            proc.wait(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            try:
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=10)
            except (OSError, subprocess.TimeoutExpired):
                proc.kill()
                proc.wait()
    if _state["tmpdir"]:
        shutil.rmtree(_state["tmpdir"], ignore_errors=True)
    _state.update(socket=None, proc=None, modules=[], tmpdir=None)


@app.tool()
async def load_program(bitcode_paths: list[str],
                       analysis_config: dict[str, Any] | None = None) -> dict[str, Any]:
    """Load LLVM bitcode module(s) into a fresh svf-harness daemon.

    Builds the analysis state (SVFIR, Andersen points-to, SVFG) once; the 28
    query tools then answer from it. Replaces any previously loaded daemon.
    Optional analysis_config is forwarded to svf-harness --analysis-config.
    Returns the program summary plus analysis_config, socket_path, and modules.
    """
    async with _get_lifecycle_lock():
        paths = [os.path.abspath(p) for p in bitcode_paths]
        missing = [p for p in paths if not os.path.isfile(p)]
        if missing:
            return {"error": f"bitcode file(s) not found: {missing}"}
        binary = os.environ.get("SVF_HARNESS_BIN", "svf-harness")
        if not os.path.isfile(binary) and shutil.which(binary) is None:
            return {"error": f"svf-harness binary not found (got {binary!r}); "
                             "set SVF_HARNESS_BIN=/path/to/Release-build/bin/svf-harness "
                             "or put svf-harness on PATH"}
        await anyio.to_thread.run_sync(_shutdown_daemon)
        tmpdir = tempfile.mkdtemp(prefix="svf-mcp-")
        sock = os.path.join(tmpdir, f"svf-{os.getpid()}.sock")
        log_path = os.path.join(tmpdir, "daemon.log")
        cmd = [binary, "serve", *paths, "--socket", sock]
        if analysis_config is not None:
            cmd += ["--analysis-config", json.dumps(analysis_config)]
        with open(log_path, "wb") as log:
            proc = subprocess.Popen(cmd, stdout=log, stderr=log,
                                    preexec_fn=_set_pdeathsig)
        _state.update(proc=proc, tmpdir=tmpdir, modules=paths)
        deadline = time.time() + _LOAD_TIMEOUT_S
        while not os.path.exists(sock):
            if proc.poll() is not None:
                with open(log_path, errors="replace") as f:
                    tail = f.read()[-4000:]
                _shutdown_daemon()
                return {"error": f"daemon exited with code {proc.returncode} "
                                 f"before listening; output:\n{tail}"}
            if time.time() > deadline:
                await anyio.to_thread.run_sync(_shutdown_daemon)
                return {"error": f"daemon did not create socket within "
                                 f"{_LOAD_TIMEOUT_S}s (analysis too slow?); "
                                 f"modules: {paths}"}
            await anyio.sleep(_POLL_S)
        _state["socket"] = sock
        summary = await anyio.to_thread.run_sync(lambda: _rpc("summary", {}, _LOAD_TIMEOUT_S))
        if not isinstance(summary, dict):
            summary = {"raw": summary}
        if "error" in summary:
            return summary
        active_config = await anyio.to_thread.run_sync(
            lambda: _rpc("analysis_config", {}, _LOAD_TIMEOUT_S))
        if isinstance(active_config, dict) and "error" in active_config:
            return active_config
        return {**summary, "analysis_config": active_config,
                "socket_path": sock, "modules": paths}


@app.tool()
async def unload_program() -> dict[str, Any]:
    """Shut down the current svf-harness daemon and clean up its socket."""
    async with _get_lifecycle_lock():
        had = _state["socket"] is not None or _state["proc"] is not None
        await anyio.to_thread.run_sync(_shutdown_daemon)
        return {"ok": True, "was_loaded": had}


# The 28 daemon query methods, exposed as one thin wrapper tool each.
# Design (see README.md): tools are registered statically at import time so
# MCP clients see them on connect, but each carries only a short docstring —
# the daemon's `schema` tool is the single authoritative source of truth for
# params and return shapes. Each tool takes one generic `params` dict that is
# forwarded verbatim as JSON-RPC params, so C++-side param evolution never
# requires touching this wrapper.
#
# IMPORTANT — argument nesting: always pass arguments nested under "params":
#   {"params": {"func": "main"}}   ← correct
#   {"func": "main"}               ← silently drops all params (pydantic flattens)
_ANCHOR = ("var anchors: {file,line[,name]} | {func,ret:true} | {func,arg:N}")
_METHODS: dict[str, str] = {
    "schema": "Self-describing registry: all methods with param/return docs, "
              "node/edge kinds, evidence record format. "
              'Arguments go nested under "params": {} (none). '
              "Call this first — it is the authoritative contract.",
    "summary": "Program size counts (functions, icfg/pag/svfg nodes). "
               'Arguments go nested under "params": {} (none).',
    "functions": 'Search functions by regex. Arguments go nested under "params": '
                 '{"params": {"pattern": "<ECMAScript regex, substring search>"}}. '
                 "See the `schema` tool for the authoritative contract.",
    "callers": "Callers of a function with callsite evidence. "
               'Arguments go nested under "params": '
               '{"params": {"func": "<exact function name>"}}. '
               "See the `schema` tool for the authoritative contract.",
    "callees": "Callees of a function (direct + resolved indirect). "
               'Arguments go nested under "params": '
               '{"params": {"func": "<exact function name>"}}. '
               "See the `schema` tool for the authoritative contract.",
    "cfg": "Control-flow graph of one function (nodes/edges with evidence). "
           'Arguments go nested under "params": '
           '{"params": {"func": "<exact function name>"}}. '
           "See the `schema` tool for the authoritative contract.",
    "defuse": f"Def/use statements of a variable. "
              f'Arguments go nested under "params": '
              f'{{"params": {{"var": {_ANCHOR}}}}}. '
              "See the `schema` tool for the authoritative contract.",
    "pts": f"Andersen points-to set of a variable. "
           f'Arguments go nested under "params": '
           f'{{"params": {{"var": {_ANCHOR}}}}}. '
           "See the `schema` tool for the authoritative contract.",
    "aliases": f"May-aliases of a variable (same-function scope). "
               f'Arguments go nested under "params": '
               f'{{"params": {{"var": {_ANCHOR}}}}}. '
               "See the `schema` tool for the authoritative contract.",
    "cfl_pts": f"CFLAlias-backed points-to set of a variable. "
               f'Arguments go nested under "params": '
               f'{{"params": {{"var": {_ANCHOR}}}}}. '
               "See the `schema` tool for the authoritative contract.",
    "cfl_aliases": f"CFLAlias-backed may-aliases of a variable "
                   f"(same-function scope). "
                   f'Arguments go nested under "params": '
                   f'{{"params": {{"var": {_ANCHOR}}}}}. '
                   "See the `schema` tool for the authoritative contract.",
    "dda_pts": f"FlowDDA-backed demand-driven points-to set of a variable. "
               f'Arguments go nested under "params": '
               f'{{"params": {{"var": {_ANCHOR}}}}}. '
               "See the `schema` tool for the authoritative contract.",
    "dda_aliases": f"FlowDDA-backed demand-driven may-aliases of a variable "
                   f"(same-function scope). "
                   f'Arguments go nested under "params": '
                   f'{{"params": {{"var": {_ANCHOR}}}}}. '
                   "See the `schema` tool for the authoritative contract.",
    "saber_leaks": "SABER memory-leak checker summary. "
                   'Arguments go nested under "params": {} (none). '
                   "See the `schema` tool for the authoritative contract.",
    "saber_double_frees": "SABER double-free checker summary. "
                          'Arguments go nested under "params": {} (none). '
                          "See the `schema` tool for the authoritative contract.",
    "saber_file_leaks": "SABER file open/close checker summary. "
                        'Arguments go nested under "params": {} (none). '
                        "See the `schema` tool for the authoritative contract.",
    "mta_summary": "MTA thread creation / MHP summary: fork sites, join sites, "
                   "TCT thread counts, and representative thread records. "
                   'Arguments go nested under "params": {} (none). '
                   "See the `schema` tool for the authoritative contract.",
    "mta_mhp": "MTA may-happen-in-parallel query between two source-location "
               "anchors. "
               'Arguments go nested under "params": '
               '{"params": {"left": {"file": "foo.c", "line": 10}, '
               '"right": {"file": "foo.c", "line": 20}}}. '
               "See the `schema` tool for the authoritative contract.",
    "ae_summary": "Abstract Execution trace/state-size summary. "
                  'Arguments go nested under "params": {} (none). '
                  "See the `schema` tool for the authoritative contract.",
    "ae_state": "Abstract Execution state at a source-location anchor. "
                'Arguments go nested under "params": '
                '{"params": {"at": {"file": "foo.c", "line": 10}, '
                '"limit": 50}}. '
                "See the `schema` tool for the authoritative contract.",
    "vfpath": f"Value-flow witness paths source→sink over the SVFG. "
              f'Arguments go nested under "params": '
              f'{{"params": {{"source": {_ANCHOR}, "sink": {_ANCHOR}, '
              f'"k": "<number of witness paths, 1-10, default 1>", '
              f'"max_steps": "<per-path step cap, 10-500, elided in the middle beyond it>"}}}}. '
              "See the `schema` tool for the authoritative contract.",
    "reachable": f"Batch value-flow reachability source→sinks (≤20). "
                 f'Arguments go nested under "params": '
                 f'{{"params": {{"source": {_ANCHOR}, "sinks": [{_ANCHOR}]}}}}. '
                 "See the `schema` tool for the authoritative contract.",
    "graphs": "List available graph surfaces and counts. "
              'Arguments go nested under "params": {} (none). '
              "See the `schema` tool for the authoritative contract.",
    "graph_nodes": "Browse nodes in icfg/svfg/svfir/callgraph. "
                   'Arguments go nested under "params": '
                   '{"params": {"graph": "svfg", "kind": "LoadVFGNode", '
                   '"limit": 20}}. '
                   "See the `schema` tool for the authoritative contract.",
    "graph_edges": "Browse edges in icfg/svfg/svfir/callgraph. "
                   'Arguments go nested under "params": '
                   '{"params": {"graph": "icfg", "kind": "IntraCFGEdge", '
                   '"limit": 20}}. '
                   "See the `schema` tool for the authoritative contract.",
    "node": "Fetch one graph node by graph and id. "
            'Arguments go nested under "params": '
            '{"params": {"graph": "svfg", "id": 68}}. '
            "See the `schema` tool for the authoritative contract.",
    "neighbors": "Fetch incoming/outgoing edges around one graph node. "
                 'Arguments go nested under "params": '
                 '{"params": {"graph": "svfg", "id": 68, "direction": "both"}}. '
                 "See the `schema` tool for the authoritative contract.",
    "analysis_config": "Active analysis configuration and planned precision surfaces. "
                       'Arguments go nested under "params": {} (none). '
                       "See the `schema` tool for the authoritative contract.",
}


def _make_tool(method: str):
    async def tool(params: dict[str, Any] = {}) -> dict[str, Any]:
        # blocking socket round trip runs off the event loop: queries on big
        # programs can take a while and must not stall MCP keepalives
        return await anyio.to_thread.run_sync(lambda: _rpc(method, params))
    tool.__name__ = method
    return tool


for _name, _doc in _METHODS.items():
    app.add_tool(_make_tool(_name), name=_name, description=_doc)


if __name__ == "__main__":
    # Clean shutdown on SIGTERM so the finally block runs and the daemon is
    # torn down even when the process is killed by the MCP host.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        app.run()  # stdio transport
    finally:
        _shutdown_daemon()
