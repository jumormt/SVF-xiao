#!/usr/bin/env python3
"""Check svf-harness mdBook coverage against the live schema."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


def load_methods(schema_path: Path) -> list[str]:
    data = json.loads(schema_path.read_text(encoding="utf-8"))
    methods = data.get("methods")
    if not isinstance(methods, list):
        raise SystemExit(f"{schema_path}: expected top-level methods array")
    names: list[str] = []
    for method in methods:
        name = method.get("name") if isinstance(method, dict) else None
        if not isinstance(name, str) or not name:
            raise SystemExit(f"{schema_path}: malformed method entry {method!r}")
        names.append(name)
    return sorted(set(names))


def markdown_text(book_dir: Path) -> str:
    src_dir = book_dir / "src"
    if not src_dir.is_dir():
        raise SystemExit(f"{src_dir}: missing mdBook src directory")
    parts: list[str] = []
    for path in sorted(src_dir.rglob("*.md")):
        parts.append(path.read_text(encoding="utf-8"))
    if not parts:
        raise SystemExit(f"{src_dir}: no Markdown files found")
    return "\n".join(parts)


def check_summary_links(book_dir: Path) -> list[str]:
    src_dir = book_dir / "src"
    summary = src_dir / "SUMMARY.md"
    if not summary.is_file():
        return [f"{summary}: missing SUMMARY.md"]
    text = summary.read_text(encoding="utf-8")
    problems: list[str] = []
    for match in re.finditer(r"\[[^\]]+\]\(([^)]+\.md)\)", text):
        target = match.group(1)
        if "://" in target or target.startswith("#"):
            continue
        target_path = (src_dir / target).resolve()
        try:
            target_path.relative_to(src_dir.resolve())
        except ValueError:
            problems.append(f"{summary}: link escapes src directory: {target}")
            continue
        if not target_path.is_file():
            problems.append(f"{summary}: missing linked file: {target}")
    return problems


REQUIRED_ENTRY_SECTIONS = [
    "Purpose",
    "Parameters",
    "Result",
    "Example",
    "Interpretation",
    "Follow-ups",
]


def cookbook_entries(book_dir: Path) -> dict[str, str]:
    cookbook = book_dir / "src" / "cookbook.md"
    if not cookbook.is_file():
        raise SystemExit(f"{cookbook}: missing cookbook.md")
    text = cookbook.read_text(encoding="utf-8")
    matches = list(re.finditer(r"^## `([^`]+)`\s*$", text, flags=re.MULTILINE))
    entries: dict[str, str] = {}
    for idx, match in enumerate(matches):
        name = match.group(1)
        start = match.end()
        end = matches[idx + 1].start() if idx + 1 < len(matches) else len(text)
        entries[name] = text[start:end]
    return entries


def check_cookbook(book_dir: Path, methods: list[str]) -> list[str]:
    problems: list[str] = []
    try:
        entries = cookbook_entries(book_dir)
    except SystemExit as exc:
        return [str(exc)]

    missing_entries = [name for name in methods if name not in entries]
    if missing_entries:
        problems.append("missing cookbook entries: " + ", ".join(missing_entries))

    extra_entries = sorted(set(entries) - set(methods))
    if extra_entries:
        problems.append("cookbook entries not in live schema: " + ", ".join(extra_entries))

    for name in methods:
        body = entries.get(name)
        if body is None:
            continue
        for section in REQUIRED_ENTRY_SECTIONS:
            if not re.search(rf"^### {re.escape(section)}\s*$", body, flags=re.MULTILINE):
                problems.append(f"cookbook entry `{name}` missing section: {section}")
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--book", required=True, type=Path)
    args = parser.parse_args(argv)

    methods = load_methods(args.schema)
    text = markdown_text(args.book)
    missing = [name for name in methods if f"`{name}`" not in text]
    problems = check_summary_links(args.book)
    problems.extend(check_cookbook(args.book, methods))

    if missing:
        problems.append("missing method coverage: " + ", ".join(missing))

    if problems:
        for problem in problems:
            print(f"ERROR: {problem}", file=sys.stderr)
        return 1

    print(f"coverage ok: {len(methods)} methods covered with cookbook entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
