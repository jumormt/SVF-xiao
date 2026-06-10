//===- AnchorDoc.h -- single source of truth for var-anchor prose --------===//
//
// The "variable anchor" accepted-forms text, shared by resolveVars()/
// resolveSinkNodes() error messages (Queries.cpp, VFPath.cpp) and the schema
// parameter docs (Schema.cpp), so the two can never drift apart (Task 4.3
// review carry-over).
//
//===----------------------------------------------------------------------===//
#pragma once

inline constexpr const char kAnchorForms[] =
    "accepted var anchor forms (use exactly ONE shape): "
    "{\"file\": \"demo.c\", \"line\": 8} — all values defined at that source "
    "line (add \"name\": \"b\" to filter by LLVM value-name substring); "
    "{\"func\": \"malloc\", \"ret\": true} — the return value at every "
    "callsite of func; "
    "{\"func\": \"memcpy\", \"arg\": 0} — the actual argument at every "
    "callsite of func";
