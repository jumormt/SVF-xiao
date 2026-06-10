#!/usr/bin/env bash
# 05-real-world.sh — companion script for docs/tutorials/05-real-world-program.md
#
# The harness on a real program: GNU bc from Test-Suite (crux-bc/bc.bc,
# 187 functions, compiled WITHOUT debug info — empty locs are part of the
# lesson):
#   serve (timed) -> summary -> functions "alloc|free" -> callers free
#   -> vfpath(malloc ret -> free arg0) -> pts(malloc ret)
#
# Skips cleanly (exit 0) when Test-Suite is not cloned.
#
# Optional stretch (OFF by default — ~60 s load, ~2.7 GB RSS):
#   RUN_BIG=1 bash 05-real-world.sh
# adds bash.bc: the xmalloc -> sh_xmalloc wrapper lesson + a 100+-callsite
# truncated callers result + vfpath across a 611k-node SVFG.
#
# Test hook: SVF_EX05_BC / SVF_EX05_BASH_BC override the bitcode paths
# (used to exercise the skip branch without moving Test-Suite).
#
# Requirements (the caller is expected to have sourced setup.sh):
#   - SVF_HARNESS_BIN (default: <repo>/Release-build/bin/svf-harness)
#   - python3 on PATH
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BIN="${SVF_HARNESS_BIN:-$REPO_ROOT/Release-build/bin/svf-harness}"
BC_BC="${SVF_EX05_BC:-$REPO_ROOT/Test-Suite/test_cases_bc/crux-bc/bc.bc}"
BASH_BC="${SVF_EX05_BASH_BC:-$REPO_ROOT/Test-Suite/test_cases_bc/crux-bc/bash.bc}"

if [ ! -f "$BC_BC" ]; then
    echo "SKIP: Test-Suite not cloned (see Harness README)"
    exit 0
fi

[ -x "$BIN" ] || { echo "error: svf-harness not found at $BIN (set SVF_HARNESS_BIN)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "error: python3 not on PATH" >&2; exit 1; }

WORKDIR="$(mktemp -d /tmp/svf-harness-ex05.XXXXXX)"
SOCK="$WORKDIR/harness.sock"
DAEMON_PID=""

stop_daemon() {
    if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
        "$BIN" shutdown --socket "$SOCK" >/dev/null 2>&1 || true
        for _ in $(seq 1 100); do
            if ! kill -0 "$DAEMON_PID" 2>/dev/null; then break; fi
            sleep 0.1
        done
        kill -9 "$DAEMON_PID" 2>/dev/null || true
    fi
    DAEMON_PID=""
}

cleanup() {
    stop_daemon
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

# start_daemon <bitcode> <max-wait-seconds>
start_daemon() {
    rm -f "$SOCK"
    SECONDS=0
    "$BIN" serve "$1" --socket "$SOCK" >"$WORKDIR/daemon.log" 2>&1 &
    DAEMON_PID=$!
    local i
    for i in $(seq 1 $(( $2 * 10 ))); do
        if [ -S "$SOCK" ]; then break; fi
        if ! kill -0 "$DAEMON_PID" 2>/dev/null; then break; fi
        sleep 0.1
    done
    if [ ! -S "$SOCK" ]; then
        echo "error: daemon failed to start on $1; daemon.log:" >&2
        cat "$WORKDIR/daemon.log" >&2
        exit 1
    fi
    echo "daemon ready in ${SECONDS}s (pid $DAEMON_PID)"
}

step() { printf '\n### %s\n' "$*"; }

# ---------------------------------------------------------------------------
# Part 1: GNU bc (bc.bc, 187 functions) — always runs
# ---------------------------------------------------------------------------

step "Serve bc.bc (real program, no recompile needed — Test-Suite ships bitcode)"
start_daemon "$BC_BC" 120

step "summary — two orders of magnitude bigger than demo.c"
SUMMARY_JSON="$("$BIN" summary --socket "$SOCK")"
printf '%s\n' "$SUMMARY_JSON" | python3 -m json.tool
printf '%s' "$SUMMARY_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
assert j["functions"] == 187, f"bc.bc should have 187 functions: {j}"
for key in ("icfg_nodes", "pag_nodes", "svfg_nodes"):
    assert j[key] > 10000, f"summary.{key} suspiciously small: {j}"
nsvfg = j["svfg_nodes"]
print(f"ok: 187 functions, {nsvfg} SVFG nodes")
'

step 'functions pattern "alloc|free" — map the allocation surface'
FUNCS_JSON="$("$BIN" functions --params '{"pattern": "alloc|free"}' --socket "$SOCK")"
printf '%s\n' "$FUNCS_JSON" | python3 -m json.tool
printf '%s' "$FUNCS_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
names = {f["name"] for f in j["functions"]}
for want in ("malloc", "free", "bc_malloc", "bc_free_num"):
    assert want in names, f"functions missing {want}: {sorted(names)}"
print("ok: malloc, free, bc_malloc, bc_free_num all found")
# bc.bc was compiled WITHOUT -g: every loc is empty, even for definitions
assert all(f["loc"]["file"] == "" and f["loc"]["line"] == 0
           for f in j["functions"]), "expected empty locs (no debug info)"
print("ok: all locs empty — crux-bc bitcode carries no debug info")
'

step "callers free — every place bc releases memory"
CALLERS_JSON="$("$BIN" callers --params '{"func": "free"}' --socket "$SOCK")"
printf '%s' "$CALLERS_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
total = j["total"]
assert total == 35, f"bc.bc has 35 free callsites, got {total}"
assert not j["truncated"]
funcs = sorted({c["caller"] for c in j["calls"]})
assert all(c["direct"] for c in j["calls"]), "bc calls free directly only"
# no debug info, but evidence still anchors us: loc.func + ir
assert all(c["callsite"]["loc"]["func"] for c in j["calls"])
assert all("call void @free" in c["callsite"]["ir"] for c in j["calls"])
print(f"ok: 35 callsites of free across {len(funcs)} functions, all direct")
print("    callers:", " ".join(funcs))
print("    sample callsite evidence:")
print(json.dumps(j["calls"][0], indent=2))
'

step "vfpath malloc-ret -> free-arg0, k=2 — heap lifecycle witnesses"
VFPATH_JSON="$("$BIN" vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"func": "free", "arg": 0}, "k": 2}' \
    --socket "$SOCK")"
printf '%s\n' "$VFPATH_JSON" | python3 -m json.tool
printf '%s' "$VFPATH_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
assert len(j["paths"]) >= 1, f"expected at least one malloc->free path: {j}"
p = j["paths"][0]
assert p["steps"][0]["edge"] is None
assert p["steps"][0]["node"]["kind"] == "AddrVFGNode", p["steps"][0]
assert "malloc" in p["steps"][0]["node"]["ir"]
npaths, nsrc, nsink = len(j["paths"]), j["sources"], j["sinks"]
where = p["steps"][0]["node"]["loc"]["func"]
print(f"ok: {npaths} witness path(s); {nsrc} malloc sources, "
      f"{nsink} free-arg sink nodes; first path starts at the malloc "
      f"AddrVFGNode in {where}")
'

step "pts on the malloc return values — one HeapObjVar per callsite"
PTS_JSON="$("$BIN" pts --params '{"var": {"func": "malloc", "ret": true}}' --socket "$SOCK")"
printf '%s' "$PTS_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
assert j["total"] >= 1
for v in j["vars"]:
    assert v["points_to"], f"malloc ret with empty pts: {v}"
    assert any(t["kind"] == "HeapObjVar" for t in v["points_to"]), v
total = j["total"]
print(f"ok: {total} malloc callsites, each return value points to "
      "its own HeapObjVar")
print("sample:")
print(json.dumps(j["vars"][0], indent=2))
'

step "Shut down the bc.bc daemon"
stop_daemon
echo "daemon stopped"

# ---------------------------------------------------------------------------
# Part 2 (RUN_BIG=1): bash.bc — the xmalloc wrapper lesson at scale
# ---------------------------------------------------------------------------

if [ "${RUN_BIG:-0}" = "1" ]; then
    if [ ! -f "$BASH_BC" ]; then
        echo "error: RUN_BIG=1 but $BASH_BC not found" >&2
        exit 1
    fi

    step "Serve bash.bc (2368 functions — expect ~60 s and ~2.7 GB RSS)"
    start_daemon "$BASH_BC" 600

    step "callers xmalloc — zero! (this bash wraps allocation in sh_xmalloc)"
    XM_JSON="$("$BIN" callers --params '{"func": "xmalloc"}' --socket "$SOCK")"
    printf '%s\n' "$XM_JSON" | python3 -m json.tool
    printf '%s' "$XM_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
assert j["total"] == 0, f"expected 0 xmalloc callers in this bash build: {j}"
print("ok: 0 callers — not a bug, a naming assumption to correct")
'

    step 'functions pattern "xmalloc" — discover the real wrapper'
    FN_JSON="$("$BIN" functions --params '{"pattern": "xmalloc"}' --socket "$SOCK")"
    printf '%s\n' "$FN_JSON" | python3 -m json.tool
    printf '%s' "$FN_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
names = {f["name"] for f in j["functions"]}
assert "sh_xmalloc" in names, f"expected sh_xmalloc: {sorted(names)}"
print("ok: sh_xmalloc found")
'

    step "callers sh_xmalloc — hits the 200-row cap (truncated: true)"
    SHX_JSON="$("$BIN" callers --params '{"func": "sh_xmalloc"}' --socket "$SOCK")"
    printf '%s' "$SHX_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
total, shown = j["total"], len(j["calls"])
assert j["truncated"] is True, f"expected truncated callers list: total={total}"
print(f"ok: {total} callsites, {shown} returned, truncated=true")
'

    step "vfpath sh_xmalloc-ret -> sh_xfree-arg0 over a 611k-node SVFG"
    BV_JSON="$("$BIN" vfpath --params \
        '{"source": {"func": "sh_xmalloc", "ret": true}, "sink": {"func": "sh_xfree", "arg": 0}, "k": 1}' \
        --socket "$SOCK")"
    printf '%s' "$BV_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
assert len(j["paths"]) >= 1, f"expected a sh_xmalloc->sh_xfree path: {j}"
length = j["paths"][0]["length"]
nsrc, nsink, visited = j["sources"], j["sinks"], j["visited"]
print(f"ok: witness path of length {length} "
      f"({nsrc} sources, {nsink} sinks, visited {visited})")
'

    step "Shut down the bash.bc daemon"
    stop_daemon
    echo "daemon stopped"
else
    step "Skipping bash.bc stretch (set RUN_BIG=1 to include it — ~60 s load)"
fi

echo
echo "EXAMPLE 05 PASSED"
