#!/usr/bin/env python3
"""Invariant: Schema.cpp node_kinds == the name prefixes printed by the SVF
node toString() implementations. Pure text analysis, no build needed."""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA = os.path.join(HERE, "..", "Schema.cpp")
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SOURCES = [os.path.join(ROOT, "svf", "lib", p) for p in
           ("Graphs/ICFG.cpp", "Graphs/VFG.cpp", "Graphs/SVFG.cpp",
            "SVFIR/SVFVariables.cpp")]

def schema_kinds():
    with open(SCHEMA) as f:
        return set(re.findall(r'nodeKind\("([A-Za-z0-9_]+)"', f.read()))

def tostring_kinds():
    kinds = set()
    for path in SOURCES:
        with open(path) as f:
            text = f.read()
        # One chunk per top-level function definition; a toString body runs
        # until the next definition starts at column 0.
        for chunk in re.split(r"(?m)^(?=[A-Za-z_][^\n=;]*::~?\w+\s*\()", text):
            if "::toString" not in chunk.split("(", 1)[0]:
                continue
            # Candidate literal: an identifier right after the opening quote,
            # ended by quote/space/colon (drops mid-string tokens like "MR_").
            lits = list(re.finditer(
                r'rawstr\s*<<\s*"([A-Za-z][A-Za-z0-9]*)(?=["\s:])', chunk))
            for i, m in enumerate(lits):
                # Keep only LEADING literals: the first of the body, or the
                # first of an if/else branch — this collects both alias names
                # of InterPHIVFGNode (FormalParmPHI/ActualRetPHI) and
                # InterMSSAPHISVFGNode (FormalINPHI.../ActualOUTPHI...) while
                # dropping later same-body output like "SVFStmt: [".
                before = chunk[:m.start()].rstrip()
                if i and not (before.endswith(")") or before.endswith("else")):
                    continue
                name = m.group(1)
                if not name.endswith("Edge"):  # edge_kinds live elsewhere
                    kinds.add(name)
    return kinds

def main():
    schema, printed = schema_kinds(), tostring_kinds()
    missing, extra = sorted(printed - schema), sorted(schema - printed)
    if missing:
        print("toString kinds missing from Schema.cpp:", ", ".join(missing))
    if extra:
        print("Schema.cpp kinds with no toString prefix:", ", ".join(extra))
    if missing or extra:
        return 1
    print(f"OK: {len(schema)} node kinds consistent")
    return 0

if __name__ == "__main__":
    sys.exit(main())
