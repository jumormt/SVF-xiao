#!/usr/bin/env bash
# llm_workflow.sh — end-to-end demo of the svf-harness LLM workflow.
#
# Replays the query sequence an LLM would issue against the daemon:
#   1. schema                    — what can I ask, and what do answers look like?
#   2. functions(".*free.*")     — find candidate functions by name
#   3. vfpath(malloc ret → b[0]) — get a value-flow witness path with evidence
#
# Requirements (the caller is expected to have sourced setup.sh):
#   - SVF_HARNESS_BIN (default: <repo>/Release-build/bin/svf-harness)
#   - clang and python3 on PATH
#
# Exits 0 and prints "DEMO PASSED" iff the witness path crosses the make_buf
# call boundary (RetDirSVFGEdge) and reaches demo.c line 11 (return b[0]).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BIN="${SVF_HARNESS_BIN:-$REPO_ROOT/Release-build/bin/svf-harness}"
FIXTURE="$SCRIPT_DIR/../tests/fixtures/demo.c"

[ -x "$BIN" ] || { echo "error: svf-harness not found at $BIN (set SVF_HARNESS_BIN)" >&2; exit 1; }
command -v clang  >/dev/null || { echo "error: clang not on PATH (source setup.sh first)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "error: python3 not on PATH" >&2; exit 1; }

WORKDIR="$(mktemp -d /tmp/svf-harness-demo.XXXXXX)"
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

step "Compile fixture demo.c to LLVM IR"
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o "$WORKDIR/demo.ll" "$FIXTURE"
echo "built $WORKDIR/demo.ll"

step "Start daemon: svf-harness serve demo.ll --socket <tmp>"
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

step "1. schema — discover the query surface (method names + node kind count)"
"$BIN" schema --socket "$SOCK" | python3 -c '
import json, sys
j = json.load(sys.stdin)
print("methods:   ", ", ".join(m["name"] for m in j["methods"]))
print("node_kinds:", len(j["node_kinds"]), "documented evidence kinds")
'

step '2. functions(pattern=".*free.*") — find candidate functions by regex'
"$BIN" functions --params '{"pattern": ".*free.*"}' --socket "$SOCK" \
    | python3 -m json.tool

step "3. vfpath(source = malloc return value, sink = demo.c:11) — witness path"
VFPATH_JSON="$("$BIN" vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"file": "demo.c", "line": 11}, "k": 1}' \
    --socket "$SOCK")"
printf '%s\n' "$VFPATH_JSON" | python3 -m json.tool

step "Assertions"
if ! printf '%s' "$VFPATH_JSON" | grep -q "RetDirSVFGEdge"; then
    echo "FAIL: witness path has no RetDirSVFGEdge (make_buf call boundary)" >&2
    exit 1
fi
echo "ok: path crosses the make_buf call boundary (RetDirSVFGEdge)"
printf '%s' "$VFPATH_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
steps = j["paths"][0]["steps"]
assert steps[0]["node"]["loc"]["line"] == 4, \
    "first step does not locate the malloc (demo.c line 4)"
print("ok: first step locates the malloc in make_buf (demo.c line 4)")
assert any(s["node"]["loc"]["line"] == 11 for s in steps), \
    "no step at demo.c line 11"
print("ok: witness path reaches demo.c line 11 (return b[0])")
'

step "Shut down daemon"
"$BIN" shutdown --socket "$SOCK" >/dev/null
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
echo "daemon stopped"

echo
echo "DEMO PASSED"
