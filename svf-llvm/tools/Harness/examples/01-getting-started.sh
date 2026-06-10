#!/usr/bin/env bash
# 01-getting-started.sh — companion script for docs/tutorials/01-getting-started.md
#
# The smallest possible svf-harness session:
#   compile demo.c to LLVM IR -> serve -> summary -> functions -> shutdown
#
# Requirements (the caller is expected to have sourced setup.sh):
#   - SVF_HARNESS_BIN (default: <repo>/Release-build/bin/svf-harness)
#   - clang and python3 on PATH
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BIN="${SVF_HARNESS_BIN:-$REPO_ROOT/Release-build/bin/svf-harness}"
FIXTURE="$(cd "$SCRIPT_DIR/../tests/fixtures" && pwd)/demo.c"

[ -x "$BIN" ] || { echo "error: svf-harness not found at $BIN (set SVF_HARNESS_BIN)" >&2; exit 1; }
command -v clang   >/dev/null || { echo "error: clang not on PATH (source setup.sh first)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "error: python3 not on PATH" >&2; exit 1; }

WORKDIR="$(mktemp -d /tmp/svf-harness-ex01.XXXXXX)"
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

step "Compile demo.c to LLVM IR (debug info + value names = good source locs)"
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o "$WORKDIR/demo.ll" "$FIXTURE"
echo "built $WORKDIR/demo.ll"

step "Start the daemon: svf-harness serve demo.ll --socket <tmp>"
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

step "summary — sizes of the graphs the daemon built"
SUMMARY_JSON="$("$BIN" summary --socket "$SOCK")"
printf '%s\n' "$SUMMARY_JSON" | python3 -m json.tool

step "functions — list every function the analysis sees"
FUNCS_JSON="$("$BIN" functions --socket "$SOCK")"
printf '%s\n' "$FUNCS_JSON" | python3 -m json.tool

step "Assertions"
printf '%s' "$SUMMARY_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
for key in ("functions", "icfg_nodes", "pag_nodes", "svfg_nodes"):
    assert j.get(key, 0) > 0, f"summary.{key} missing or zero: {j}"
nfun, nsvfg = j["functions"], j["svfg_nodes"]
print(f"ok: summary reports non-empty graphs ({nfun} functions, {nsvfg} SVFG nodes)")
'
printf '%s' "$FUNCS_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
names = {f["name"] for f in j["functions"]}
for want in ("make_buf", "use_after_free", "main", "malloc", "free"):
    assert want in names, f"functions list is missing {want}: {sorted(names)}"
print("ok: make_buf, use_after_free, main, malloc, free are all listed")
free_entry = [f for f in j["functions"] if f["name"] == "free"][0]
assert free_entry["is_decl"], f"free should be an external declaration: {free_entry}"
assert free_entry["loc"]["file"] == "", f"declaration loc should be empty: {free_entry}"
print("ok: free is flagged is_decl=true (external declaration, empty loc)")
'

step "Shut down the daemon"
"$BIN" shutdown --socket "$SOCK" >/dev/null
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
echo "daemon stopped"

echo
echo "EXAMPLE 01 PASSED"
