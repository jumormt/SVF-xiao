#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
PROJECT_ROOT = HERE.parents[3]
DEFAULT_ROOT = PROJECT_ROOT / "Test-Suite" / "test_cases_bc"
DEFAULT_BIN = os.environ.get("SVF_HARNESS_BIN", "svf-harness")


def is_original_bitcode(path):
    name = path.name
    return ".pre" not in name and not name.endswith(".svf.bc")


def parse_args():
    p = argparse.ArgumentParser(
        description="Sweep Test-Suite bitcodes through svf-harness summary.")
    p.add_argument("--root", default=str(DEFAULT_ROOT),
                   help="Test-Suite bitcode root")
    p.add_argument("--bin", default=DEFAULT_BIN,
                   help="svf-harness binary")
    p.add_argument("--limit", type=int, default=0,
                   help="maximum files to test; 0 means all")
    p.add_argument("--timeout", type=float, default=120.0,
                   help="per-file timeout in seconds")
    p.add_argument("--progress-every", type=int, default=100,
                   help="print progress every N files")
    return p.parse_args()


def main():
    args = parse_args()
    bin_path = args.bin
    if not os.path.isfile(bin_path) and shutil.which(bin_path) is None:
        print(f"error: svf-harness not found: {bin_path!r}", file=sys.stderr)
        return 2

    root = Path(args.root)
    if not root.is_dir():
        print(f"SKIP: Test-Suite bitcode root not present: {root}")
        return 0

    bitcodes = sorted(p for p in root.rglob("*.bc") if is_original_bitcode(p))
    if args.limit:
        bitcodes = bitcodes[:args.limit]
    if not bitcodes:
        print(f"SKIP: no bitcode files under {root}")
        return 0

    failures = []
    started = time.time()
    for idx, bc in enumerate(bitcodes, 1):
        rel = bc.relative_to(root)
        cmd = [bin_path, "--oneshot", "summary", str(bc)]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True,
                                 timeout=args.timeout)
        except subprocess.TimeoutExpired:
            failures.append((str(rel), "timeout", ""))
            print(f"FAIL timeout {rel}", flush=True)
            continue

        if out.returncode != 0:
            message = out.stderr.strip() or out.stdout.strip()
            try:
                message = json.loads(out.stdout)["error"]["message"]
            except Exception:
                pass
            failures.append((str(rel), out.returncode, message[:500]))
            print(f"FAIL rc={out.returncode} {rel}: {message[:200]}",
                  flush=True)
        elif args.progress_every and idx % args.progress_every == 0:
            elapsed = time.time() - started
            print(f"ok {idx}/{len(bitcodes)} elapsed={elapsed:.1f}s",
                  flush=True)

    elapsed = time.time() - started
    print(f"sweep total={len(bitcodes)} failures={len(failures)} "
          f"elapsed={elapsed:.1f}s")
    if failures:
        for rel, code, message in failures[:20]:
            print(f"failure {rel}: {code} {message}")
        if len(failures) > 20:
            print(f"... {len(failures) - 20} more failures")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
