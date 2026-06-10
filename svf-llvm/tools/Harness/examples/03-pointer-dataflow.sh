#!/usr/bin/env bash
# 03-pointer-dataflow.sh — companion script for docs/tutorials/03-pointer-and-dataflow.md
#
# Pointer and dataflow queries on demo.c, plus the three var-anchor forms:
#   pts({func,ret}) -> aliases -> defuse({file,line,name}) -> pts({func,arg})
#   -> a deliberate error showing the did-you-mean hint
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

WORKDIR="$(mktemp -d /tmp/svf-harness-ex03.XXXXXX)"
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

step "Compile demo.c and start the daemon"
clang -S -emit-llvm -g -O0 -fno-discard-value-names \
      -o "$WORKDIR/demo.ll" "$FIXTURE"
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

step 'pts({func: "malloc", ret: true}) — what does the malloc return value point to?'
PTS_JSON="$("$BIN" pts --params '{"var": {"func": "malloc", "ret": true}}' --socket "$SOCK")"
printf '%s\n' "$PTS_JSON" | python3 -m json.tool
printf '%s' "$PTS_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
objs = [o for v in j["vars"] for o in v["points_to"]]
heaps = [o for o in objs if o["kind"] == "HeapObjVar"]
assert heaps, f"no HeapObjVar in points-to set: {objs}"
assert heaps[0]["loc"]["line"] == 4, heaps[0]
print("ok: points-to set contains the HeapObjVar allocated at demo.c line 4")
'

step 'aliases({func: "malloc", ret: true}) — which vars may name the same memory?'
ALIASES_JSON="$("$BIN" aliases --params '{"var": {"func": "malloc", "ret": true}}' --socket "$SOCK")"
printf '%s\n' "$ALIASES_JSON" | python3 -m json.tool
printf '%s' "$ALIASES_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
v = j["vars"][0]
assert v["aliases"], f"expected at least one alias: {j}"
assert all(a["id"] != v["var"]["id"] for a in v["aliases"]), \
    "a var must not be listed as its own alias"
nalias = len(v["aliases"])
print(f"ok: {nalias} may-alias candidate(s), var itself excluded")
'

step 'defuse({file: "demo.c", line: 8, name: "b"}) — where is b defined and used?'
DEFUSE_JSON="$("$BIN" defuse --params '{"var": {"file": "demo.c", "line": 8, "name": "b"}}' --socket "$SOCK")"
printf '%s\n' "$DEFUSE_JSON" | python3 -m json.tool
printf '%s' "$DEFUSE_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
v = j["vars"][0]
assert v["defs"], f"expected defs for b: {v}"
use_lines = {u["at"]["loc"]["line"] for u in v["uses"]}
assert use_lines & {9, 10, 11}, f"uses should hit lines 9/10/11: {use_lines}"
ndefs = len(v["defs"])
print(f"ok: b has {ndefs} def(s); use lines: {sorted(use_lines)}")
'

step 'pts({func: "free", arg: 0}) — the THIRD anchor form: argument 0 of free'
ARG_JSON="$("$BIN" pts --params '{"var": {"func": "free", "arg": 0}}' --socket "$SOCK")"
printf '%s\n' "$ARG_JSON" | python3 -m json.tool
printf '%s' "$ARG_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
objs = [o for v in j["vars"] for o in v["points_to"]]
heaps = [o for o in objs if o["kind"] == "HeapObjVar"]
assert heaps and heaps[0]["loc"]["line"] == 4, objs
print("ok: what free() frees is exactly the heap object from demo.c line 4")
'

step 'Deliberate error: pts({func: "make_buff", ret: true}) — unknown function'
set +e
ERR_JSON="$("$BIN" pts --params '{"var": {"func": "make_buff", "ret": true}}' --socket "$SOCK")"
ERR_RC=$?
set -e
printf '%s\n' "$ERR_JSON" | python3 -m json.tool
[ "$ERR_RC" -eq 1 ] || { echo "FAIL: expected exit code 1, got $ERR_RC" >&2; exit 1; }
printf '%s' "$ERR_JSON" | python3 -c '
import json, sys
j = json.load(sys.stdin)
msg = j["message"]
assert "did you mean" in msg, f"no did-you-mean hint: {msg}"
assert "make_buf" in msg, f"hint does not suggest make_buf: {msg}"
print("ok: exit code 1, error carries a did-you-mean hint suggesting make_buf")
'

step "Shut down the daemon"
"$BIN" shutdown --socket "$SOCK" >/dev/null
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
echo "daemon stopped"

echo
echo "EXAMPLE 03 PASSED"
