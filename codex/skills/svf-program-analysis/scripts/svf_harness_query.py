#!/usr/bin/env python3
"""Small wrapper for svf-harness oneshot/schema calls."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional


def find_repo(explicit: Optional[str]) -> Path:
    if explicit:
        return Path(explicit).expanduser().resolve()
    env_repo = os.environ.get("SVF_REPO")
    if env_repo:
        repo = Path(env_repo).expanduser().resolve()
        if (repo / "Release-build" / "bin" / "svf-harness").exists():
            return repo
    cwd = Path.cwd().resolve()
    for path in [cwd, *cwd.parents]:
        if (path / "Release-build" / "bin" / "svf-harness").exists():
            return path
    raise SystemExit("cannot find SVF repo; pass --repo or set SVF_REPO")


def harness_bin(repo: Path, explicit: Optional[str]) -> str:
    if explicit:
        return explicit
    candidate = repo / "Release-build" / "bin" / "svf-harness"
    if candidate.exists():
        return str(candidate)
    found = shutil.which("svf-harness")
    if found:
        return found
    raise SystemExit("svf-harness not found; build Release-build/bin/svf-harness")


def run_json(cmd: list[str]) -> dict:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        if proc.stdout.strip():
            print(proc.stdout, end="")
        if proc.stderr.strip():
            print(proc.stderr, file=sys.stderr, end="")
        raise SystemExit(proc.returncode)
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        print(proc.stdout)
        raise SystemExit(f"non-JSON harness output: {exc}")


def compile_c(args: argparse.Namespace) -> None:
    clang = args.clang or shutil.which("clang")
    if not clang:
        raise SystemExit("clang not found; pass --clang")
    src = Path(args.source).resolve()
    out = Path(args.output).resolve() if args.output else Path(tempfile.mkstemp(suffix=".ll")[1])
    cmd = [
        clang,
        "-S",
        "-emit-llvm",
        "-g",
        "-O0",
        "-fno-discard-value-names",
        "-o",
        str(out),
        str(src),
    ]
    subprocess.check_call(cmd)
    print(out)


def schema(args: argparse.Namespace) -> None:
    repo = find_repo(args.repo)
    binary = harness_bin(repo, args.bin)
    out = run_json([binary, "--oneshot", "schema", args.bitcode])
    print(json.dumps(out, indent=2, sort_keys=True))


def oneshot(args: argparse.Namespace) -> None:
    repo = find_repo(args.repo)
    binary = harness_bin(repo, args.bin)
    params = json.loads(args.params or "{}")
    cmd = [binary, "--oneshot", args.method, "--params", json.dumps(params)]
    if args.analysis_config:
        cmd += ["--analysis-config", args.analysis_config]
    cmd.append(args.bitcode)
    out = run_json(cmd)
    print(json.dumps(out, indent=2, sort_keys=True))


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("compile-c")
    p.add_argument("source")
    p.add_argument("-o", "--output")
    p.add_argument("--clang")
    p.set_defaults(func=compile_c)

    for name, func in [("schema", schema), ("oneshot", oneshot)]:
        p = sub.add_parser(name)
        p.add_argument("--repo")
        p.add_argument("--bin")
        p.add_argument("--bitcode", required=True)
        if name == "oneshot":
            p.add_argument("--method", required=True)
            p.add_argument("--params", default="{}")
            p.add_argument("--analysis-config")
        p.set_defaults(func=func)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
