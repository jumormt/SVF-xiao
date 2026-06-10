//===- Evidence.cpp -- uniform JSON evidence records for SVF nodes -------===//
#include "Evidence.h"
#include "Graphs/ICFGNode.h"
#include "Graphs/VFGNode.h"
#include "SVFIR/SVFVariables.h"
#include <cctype>
#include <cstdlib>

using json = nlohmann::json;

namespace
{
/// "ir" field: node->toString(), truncated to 200 chars.
std::string truncatedIR(const std::string& s)
{
    constexpr size_t kMax = 200;
    if (s.size() <= kMax)
        return s;
    return s.substr(0, kMax) + "…";
}

/// "kind" field: the leading alphabetic run of toString(), which by SVF
/// convention is the node's class name (e.g. "IntraICFGNode123 {...}" ->
/// "IntraICFGNode", "LoadVFGNode ID: 5 ..." -> "LoadVFGNode", "ValVar ID: 5"
/// -> "ValVar"). Chosen over a GNodeK switch: it stays correct as subclasses
/// are added, since every toString() override prints its own class name.
std::string kindOf(const std::string& irStr)
{
    size_t i = 0;
    while (i < irStr.size() &&
           std::isalpha(static_cast<unsigned char>(irStr[i])))
        ++i;
    return i ? irStr.substr(0, i) : "Unknown";
}

/// Shared builder for all node overloads. Never throws.
json record(SVF::NodeID id, const std::string& irStr,
            const std::string& srcLoc, const SVF::FunObjVar* fun)
{
    json loc = evidence::loc(srcLoc);
    loc["func"] = fun ? fun->getName() : "";
    return json{{"kind", kindOf(irStr)},
                {"id", id},
                {"loc", std::move(loc)},
                {"ir", truncatedIR(irStr)}};
}

/// Extract the integer following `"<key>":` in an SVF source-loc string.
int intField(const std::string& s, const std::string& key)
{
    size_t pos = s.find("\"" + key + "\":");
    if (pos == std::string::npos)
        return 0;
    pos += key.size() + 3; // skip `"key":`
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
        ++pos;
    return std::atoi(s.c_str() + pos); // non-numeric -> 0
}

/// Extract the quoted string following `"<key>": "` in a source-loc string.
std::string strField(const std::string& s, const std::string& key)
{
    size_t pos = s.find("\"" + key + "\":");
    if (pos == std::string::npos)
        return "";
    pos = s.find('"', pos + key.size() + 3);
    if (pos == std::string::npos)
        return "";
    size_t end = s.find('"', pos + 1);
    if (end == std::string::npos)
        return "";
    return s.substr(pos + 1, end - pos - 1);
}
} // namespace

namespace evidence
{
json loc(const std::string& svfSourceLoc)
{
    // Instructions use "fl", functions use "file" (LLVMUtil::getSourceLoc).
    std::string file = strField(svfSourceLoc, "fl");
    if (file.empty())
        file = strField(svfSourceLoc, "file");
    return json{{"file", file}, {"line", intField(svfSourceLoc, "ln")}};
}

json node(const SVF::ICFGNode* n)
{
    return record(n->getId(), n->toString(), n->getSourceLoc(), n->getFun());
}

json node(const SVF::VFGNode* n)
{
    return record(n->getId(), n->toString(), n->getSourceLoc(), n->getFun());
}

json node(const SVF::SVFVar* n)
{
    return record(n->getId(), n->toString(), n->getSourceLoc(),
                  n->getFunction());
}
} // namespace evidence
