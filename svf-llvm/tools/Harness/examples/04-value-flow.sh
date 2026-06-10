#!/usr/bin/env bash
# 04-value-flow.sh — companion script for docs/tutorials/04-value-flow-witnesses.md
#
# Value-flow witness queries:
#   vfpath(malloc ret -> demo.c:11)        — the use-after-free witness path
#   reachable(batch of 3 sinks)            — incl. one unreachable + one bad sink
#   vfpath(max_steps=10) on chain.c        — middle elision on long paths (oneshot)
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

WORKDIR="$(mktemp -d /tmp/svf-harness-ex04.XXXXXX)"
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

step "Compile fixtures (demo.c for the daemon, chain.c for oneshot)"
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o "$WORKDIR/demo.ll" "$FIXDIR/demo.c"
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o "$WORKDIR/chain.ll" "$FIXDIR/chain.c"
echo "built demo.ll and chain.ll"

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

step "vfpath(source = malloc return, sink = demo.c:11) — the witness path"
VFPATH_JSON="$("$BIN" vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"file": "demo.c", "line": 11}, "k": 1}' \
    --socket "$SOCK")"
printf '%s\n' "$VFPATH_JSON" | python3 -m json.tool
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

step "reachable — one source, three sinks: reachable, unreachable, unresolvable"
REACH_JSON="$("$BIN" reachable --params \
    '{"source": {"func": "malloc", "ret": true},
      "sinks": [{"file": "demo.c", "line": 11},
                {"file": "demo.c", "line": 22},
                {"file": "demo.c", "line": 999}]}' \
    --socket "$SOCK")"
printf '%s\n' "$REACH_JSON" | python3 -m json.tool
printf '%s' "$REACH_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
r = j["results"]
assert len(r) == 3, r
assert r[0]["reachable"] and r[0]["first_path"]["steps"], r[0]
print("ok: sink demo.c:11 reachable, witness path attached")
assert not r[1]["reachable"] and "error" not in r[1], r[1]
print("ok: sink demo.c:22 (long_ir_helper) cleanly unreachable — no error")
assert not r[2]["reachable"] and "error" in r[2], r[2]
print("ok: sink demo.c:999 unresolvable -> per-sink error row, batch survives")
'

step "vfpath with max_steps=10 on chain.c — long-path middle elision (oneshot mode)"
CHAIN_JSON="$("$BIN" --oneshot vfpath --params \
    '{"source": {"func": "malloc", "ret": true}, "sink": {"file": "chain.c", "line": 25}, "k": 1, "max_steps": 10}' \
    "$WORKDIR/chain.ll")"
printf '%s\n' "$CHAIN_JSON" | python3 -m json.tool
printf '%s' "$CHAIN_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
p = j["paths"][0]
assert p.get("steps_truncated"), f"expected steps_truncated: {p.keys()}"
markers = [s for s in p["steps"] if "elided_steps" in s]
assert len(markers) == 1, f"expected exactly one elision marker: {len(markers)}"
assert markers[0]["elided_steps"] > 0
shown = len(p["steps"]) - len(markers)
length, hidden = p["length"], markers[0]["elided_steps"]
print(f"ok: path of length {length} elided to {shown} shown steps "
      f"+ 1 marker hiding {hidden} middle steps")
'

step "Shut down the daemon"
"$BIN" shutdown --socket "$SOCK" >/dev/null
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
echo "daemon stopped"

echo
echo "EXAMPLE 04 PASSED"
