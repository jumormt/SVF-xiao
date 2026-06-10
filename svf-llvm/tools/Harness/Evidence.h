//===- Evidence.h -- uniform JSON evidence records for SVF nodes ---------===//
#pragma once
#include "nlohmann/json.hpp"
#include <string>
namespace SVF
{
class ICFGNode;
class VFGNode;
class SVFVar;
class FunObjVar;
}
namespace evidence
{
/// Uniform record: {"kind": "...", "id": N, "loc": {"file","line","func"}, "ir": "..."}
nlohmann::json node(const SVF::ICFGNode* n);
nlohmann::json node(const SVF::VFGNode* n);
nlohmann::json node(const SVF::SVFVar* n);
/// loc-only record for functions: {"file","line"}
/// Parses SVF's getSourceLoc() strings, e.g.
///   { "ln": 7, "cl": 12, "fl": "demo.c" }   (instructions)
///   { "ln": 4, "file": "demo.c" }           (functions)
/// Tolerant: missing keys -> line 0 / file ""; never throws.
nlohmann::json loc(const std::string& svfSourceLoc);
}
