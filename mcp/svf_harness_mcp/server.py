"""svf-harness MCP server: thin stdio adapter over the svf-harness daemon socket.

Setup for Claude Code (adjust the python path to an interpreter with `mcp`
installed, e.g. /home/xiao/program/py311-mcp/bin/python):

    claude mcp add svf \
        --env SVF_HARNESS_BIN=/path/to/Release-build/bin/svf-harness \
        -- /path/to/python /path/to/SVF-xiao/mcp/svf_harness_mcp/server.py

No daemon is spawned at startup: call the `load_program` tool with bitcode
paths first; it spawns `svf-harness serve` and the 11 query tools then forward
JSON-RPC over its Unix socket. Call the `schema` tool for the authoritative
self-describing contract of every method.
"""

import json
import os
import shutil
import signal
import socket
import subprocess
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

_LOAD_TIMEOUT_S = 600   # analysis state builds once at serve; big programs are slow
_POLL_S = 0.2


def _rpc(method: str, params: dict[str, Any]) -> dict[str, Any]:
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
            s.settimeout(_LOAD_TIMEOUT_S)
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
async def load_program(bitcode_paths: list[str]) -> dict[str, Any]:
    """Load LLVM bitcode module(s) into a fresh svf-harness daemon.

    Builds the analysis state (SVFIR, Andersen points-to, SVFG) once; the 11
    query tools then answer from it. Replaces any previously loaded daemon.
    Returns the program summary plus socket_path and modules.
    """
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
    with open(log_path, "wb") as log:
        proc = subprocess.Popen([binary, "serve", *paths, "--socket", sock],
                                stdout=log, stderr=log)
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
    summary = await anyio.to_thread.run_sync(_rpc, "summary", {})
    if "error" in summary:
        return summary
    return {**summary, "socket_path": sock, "modules": paths}


@app.tool()
async def unload_program() -> dict[str, Any]:
    """Shut down the current svf-harness daemon and clean up its socket."""
    had = _state["socket"] is not None or _state["proc"] is not None
    await anyio.to_thread.run_sync(_shutdown_daemon)
    return {"ok": True, "was_loaded": had}


# The 11 daemon query methods, exposed as one thin wrapper tool each.
# Design (see README.md): tools are registered statically at import time so
# MCP clients see them on connect, but each carries only a short docstring —
# the daemon's `schema` tool is the single authoritative source of truth for
# params and return shapes. Each tool takes one generic `params` dict that is
# forwarded verbatim as JSON-RPC params, so C++-side param evolution never
# requires touching this wrapper.
_ANCHOR = ("var anchors: {file,line[,name]} | {func,ret:true} | {func,arg:N}")
_METHODS: dict[str, str] = {
    "schema": "Self-describing registry: all methods with param/return docs, "
              "node/edge kinds, evidence record format. params: {} (none). "
              "Call this first — it is the authoritative contract.",
    "summary": "Program size counts (functions, icfg/pag/svfg nodes). "
               "params: {} (none).",
    "functions": "Search functions by regex. params keys: pattern. "
                 "See the `schema` tool for the authoritative contract.",
    "callers": "Callers of a function with callsite evidence. params keys: "
               "func. See the `schema` tool for the authoritative contract.",
    "callees": "Callees of a function (direct + resolved indirect). params "
               "keys: func. See the `schema` tool for the authoritative contract.",
    "cfg": "Control-flow graph of one function (nodes/edges with evidence). "
           "params keys: func. See the `schema` tool for the authoritative contract.",
    "defuse": f"Def/use statements of a variable. params keys: var ({_ANCHOR}). "
              "See the `schema` tool for the authoritative contract.",
    "pts": f"Andersen points-to set of a variable. params keys: var ({_ANCHOR}). "
           "See the `schema` tool for the authoritative contract.",
    "aliases": f"May-aliases of a variable (same-function scope). params keys: "
               f"var ({_ANCHOR}). See the `schema` tool for the authoritative contract.",
    "vfpath": f"Value-flow witness paths source→sink over the SVFG. params keys: "
              f"source, sink ({_ANCHOR}), k, max_steps. "
              "See the `schema` tool for the authoritative contract.",
    "reachable": f"Batch value-flow reachability source→sinks (≤20). params keys: "
                 f"source, sinks ({_ANCHOR}). "
                 "See the `schema` tool for the authoritative contract.",
}


def _make_tool(method: str):
    async def tool(params: dict[str, Any] = {}) -> dict[str, Any]:
        # blocking socket round trip runs off the event loop: queries on big
        # programs can take a while and must not stall MCP keepalives
        return await anyio.to_thread.run_sync(_rpc, method, params)
    tool.__name__ = method
    return tool


for _name, _doc in _METHODS.items():
    app.add_tool(_make_tool(_name), name=_name, description=_doc)


if __name__ == "__main__":
    try:
        app.run()  # stdio transport
    finally:
        _shutdown_daemon()
