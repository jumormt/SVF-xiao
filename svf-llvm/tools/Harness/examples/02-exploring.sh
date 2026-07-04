#!/usr/bin/env bash
# 02-exploring.sh — companion script for docs/tutorials/02-exploring-a-program.md
#
# Explore a program's structure through the daemon:
#   schema -> functions(regex) -> callers/callees -> indirect callees -> cfg
#
# Uses two fixtures: demo.c (served by a daemon) and indirect.c (queried in
# --oneshot mode — no daemon, one query, exit).
#
# Requirements (the caller is expected to have sourced setup.sh):
#   - SVF_HARNESS_BIN (default: <repo>/Release-build/bin/svf-harness)
#   - clang and python3 on PATH
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BIN="${SVF_HARNESS_BIN:-$REPO_ROOT/Release-build/bin/svf-harness}"
FIXDIR="$(cd "$SCRIPT_DIR/../tests/fixtures" && pwd)"

[ -x "$BIN" ] || { echo "error: svf-harness not found at $BIN (set SVF_HARNESS_BIN)" >&2; exit 1; }
command -v clang   >/dev/null || { echo "error: clang not on PATH (source setup.sh first)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "error: python3 not on PATH" >&2; exit 1; }

WORKDIR="$(mktemp -d /tmp/svf-harness-ex02.XXXXXX)"
SOCK="$WORKDIR/harness.sock"
DAEMON_PID=""

cleanup() {
    if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
        "$BIN" shutdown --socket "$SOCK" >/dev/null 2>&1 || true
        for _ in $(seq 1 50); do
            if ! kill -0 "$DAEMON_PID" 2>/dev/null; then break; fi
            sleep 0.1
        done
        kill -9 "$DAEMON_PID" 2>/dev/null || true
    fi
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

step() { printf '\n### %s\n' "$*"; }

step "Compile fixtures (demo.c for the daemon, indirect.c for oneshot)"
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o "$WORKDIR/demo.ll" "$FIXDIR/demo.c"
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o "$WORKDIR/indirect.ll" "$FIXDIR/indirect.c"
echo "built demo.ll and indirect.ll"

step "Start daemon on demo.ll"
"$BIN" serve "$WORKDIR/demo.ll" --socket "$SOCK" \
      >"$WORKDIR/daemon.log" 2>&1 &
DAEMON_PID=$!
for _ in $(seq 1 600); do
    if [ -S "$SOCK" ]; then break; fi
    if ! kill -0 "$DAEMON_PID" 2>/dev/null; then break; fi
    sleep 0.1
done
if [ ! -S "$SOCK" ]; then
    echo "error: daemon failed to start; daemon.log:" >&2
    cat "$WORKDIR/daemon.log" >&2
    exit 1
fi
echo "daemon ready (pid $DAEMON_PID)"

step "schema — the query surface (method names + node kind count)"
"$BIN" schema --socket "$SOCK" | python3 -c '
import json, sys
j = json.load(sys.stdin)
methods = [m["name"] for m in j["methods"]]
print("methods:   ", ", ".join(methods))
print("node_kinds:", len(j["node_kinds"]), "documented evidence kinds")
print("edge_kinds:", len(j["edge_kinds"]), "documented edge kinds")
assert len(methods) == 28, f"expected 28 methods, got {len(methods)}"
nkinds = len(j["node_kinds"])
assert nkinds == 66, f"expected 66 node kinds, got {nkinds}"
print("ok: 28 methods, 66 node kinds")
'

step 'functions(pattern=".*free.*") — regex search over function names'
FUNCS_JSON="$("$BIN" functions --params '{"pattern": ".*free.*"}' --socket "$SOCK")"
printf '%s\n' "$FUNCS_JSON" | python3 -m json.tool
printf '%s' "$FUNCS_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
names = {f["name"] for f in j["functions"]}
assert names == {"free", "use_after_free"}, names
print("ok: pattern matched exactly free + use_after_free")
'

step 'callers(func="fill") — who calls fill?'
CALLERS_JSON="$("$BIN" callers --params '{"func": "fill"}' --socket "$SOCK")"
printf '%s\n' "$CALLERS_JSON" | python3 -m json.tool
printf '%s' "$CALLERS_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
calls = j["calls"]
assert any(c["caller"] == "use_after_free" and c["direct"] for c in calls), calls
print("ok: use_after_free calls fill (direct call, callsite evidence attached)")
'

step 'callees(func="use_after_free") — what does use_after_free call?'
CALLEES_JSON="$("$BIN" callees --params '{"func": "use_after_free"}' --socket "$SOCK")"
printf '%s\n' "$CALLEES_JSON" | python3 -m json.tool
printf '%s' "$CALLEES_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
callees = {c["callee"] for c in j["calls"]}
assert {"make_buf", "fill", "free"} <= callees, callees
print("ok: callees include make_buf, fill, free")
'

step 'callees(func="apply") on indirect.c — Andersen resolves the fn-pointer table (oneshot mode)'
IND_JSON="$("$BIN" --oneshot callees --params '{"func": "apply"}' "$WORKDIR/indirect.ll")"
printf '%s\n' "$IND_JSON" | python3 -m json.tool
printf '%s' "$IND_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
indirect = {c["callee"] for c in j["calls"] if not c["direct"]}
assert indirect == {"dbl", "neg"}, f"indirect callees: {indirect}"
print("ok: indirect call resolved to dbl + neg, both flagged direct:false")
'

step 'cfg(func="use_after_free") — the control-flow graph, node by node'
CFG_JSON="$("$BIN" cfg --params '{"func": "use_after_free"}' --socket "$SOCK")"
printf '%s\n' "$CFG_JSON" | python3 -m json.tool
printf '%s' "$CFG_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
assert j["function"] == "use_after_free"
assert j["total_nodes"] > 3 and not j["truncated"], j
kinds = {e["kind"] for e in j["edges"]}
assert "IntraCFGEdge" in kinds, kinds
lines = sorted({n["loc"]["line"] for n in j["nodes"] if n["loc"]["line"]})
assert {8, 9, 10, 11} <= set(lines), lines
nn, ne = j["total_nodes"], j["total_edges"]
print(f"ok: {nn} nodes / {ne} edges, source lines covered: {lines}")
'

step "Shut down the daemon"
"$BIN" shutdown --socket "$SOCK" >/dev/null
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
echo "daemon stopped"

echo
echo "EXAMPLE 02 PASSED"
