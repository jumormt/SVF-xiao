#!/usr/bin/env bash
# run_all.sh — run every tutorial example script in order and summarize.
#
# Runs 01..05 (05 is skipped with a note until it exists / when Test-Suite is
# absent — the script itself decides). Prints a per-script PASS/FAIL table and
# exits nonzero if any script fails.
#
# Requirements: same as the individual scripts (setup.sh sourced, clang +
# python3 on PATH, SVF_HARNESS_BIN or a default Release-build binary).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SCRIPTS=(
    01-getting-started.sh
    02-exploring.sh
    03-pointer-dataflow.sh
    04-value-flow.sh
    05-real-world.sh
)

declare -a NAMES RESULTS
FAILED=0

for s in "${SCRIPTS[@]}"; do
    NAMES+=("$s")
    if [ ! -f "$SCRIPT_DIR/$s" ]; then
        RESULTS+=("SKIP (not yet written)")
        continue
    fi
    echo
    echo "===================================================================="
    echo "== running $s"
    echo "===================================================================="
    if bash "$SCRIPT_DIR/$s"; then
        RESULTS+=("PASS")
    else
        RESULTS+=("FAIL")
        FAILED=1
    fi
done

echo
echo "==================== summary ===================="
for i in "${!NAMES[@]}"; do
    printf '%-28s %s\n' "${NAMES[$i]}" "${RESULTS[$i]}"
done
echo "================================================="

if [ "$FAILED" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS"
