//===- Schema.h -- self-describing schema registry for svf-harness -------===//
#pragma once
#include "nlohmann/json.hpp"

namespace schema
{
/// Full self-describing schema: node_kinds, edge_kinds, methods, evidence_record.
/// Hand-maintained registry. INVARIANT: every `kind` string Evidence.cpp can emit
/// (toString() prefixes of ICFG/VFG/SVFG/SVFVar subclasses) appears in node_kinds.
/// When SVF adds a node class with a toString() override, add it here.
/// The caller (QueryEngine::schemaQ) merges in the runtime-dependent parts:
/// per-method "implemented" flags and the "program" block.
nlohmann::json registry();
}
