//===- SaberQueries.cpp -- QueryEngine SABER checker summaries -----------===//
//
// Third-party SVF checkers report bugs through SVFBugReport. This translation
// unit adapts those reports into the harness' stable JSON surface.
//
//===----------------------------------------------------------------------===//
#include "QueryEngine.h"
#include "Evidence.h"
#include "SABER/DoubleFreeChecker.h"
#include "SABER/FileChecker.h"
#include "SABER/LeakChecker.h"
#include "Util/Options.h"
#include "Util/SVFBugReport.h"
#include "Util/cJSON.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

using namespace SVF;
using json = nlohmann::json;

namespace
{
constexpr size_t kBugCap = 200;

void ensureSaberDefaults()
{
    const_cast<Option<bool>&>(Options::PStat).setValue(false);
    const_cast<Option<bool>&>(Options::ValidateTests).setValue(false);
    const_cast<Option<bool>&>(Options::DumpSlice).setValue(false);
}

const char* eventTypeName(u32_t type)
{
    switch (type)
    {
    case SVFBugEvent::Branch:
        return "branch";
    case SVFBugEvent::Caller:
        return "caller";
    case SVFBugEvent::CallSite:
        return "callsite";
    case SVFBugEvent::Loop:
        return "loop";
    case SVFBugEvent::SourceInst:
        return "source";
    default:
        return "unknown";
    }
}

json cjsonToJson(cJSON* raw)
{
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> owned(raw, cJSON_Delete);
    if (!owned)
        return json::object();
    char* printed = cJSON_PrintUnformatted(owned.get());
    if (!printed)
        return json::object();
    std::string text(printed);
    cJSON_free(printed);
    json parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded())
        return json{{"raw", text}};
    return parsed;
}

json eventRecord(const SVFBugEvent& event)
{
    json loc = evidence::loc(event.getEventLoc());
    loc["func"] = event.getFuncName();
    return json{{"type", eventTypeName(event.getEventType())},
                {"function", event.getFuncName()},
                {"loc", std::move(loc)},
                {"description", event.getEventDescription()}};
}

json bugRecord(const GenericBug* bug)
{
    json loc = evidence::loc(bug->getLoc());
    loc["func"] = bug->getFuncName();
    json events = json::array();
    for (const SVFBugEvent& event : bug->getEventStack())
        events.push_back(eventRecord(event));
    return json{{"type", GenericBug::BugType2Str.at(bug->getBugType())},
                {"function", bug->getFuncName()},
                {"loc", std::move(loc)},
                {"events", std::move(events)},
                {"description", cjsonToJson(bug->getBugDescription())}};
}

std::unique_ptr<LeakChecker> makeChecker(const std::string& checker)
{
    if (checker == "leak")
        return std::make_unique<LeakChecker>();
    if (checker == "double-free")
        return std::make_unique<DoubleFreeChecker>();
    if (checker == "file")
        return std::make_unique<FileChecker>();
    throw std::runtime_error("unknown SABER checker: " + checker);
}
} // namespace

json QueryEngine::runSaberChecker(const std::string& checker) const
{
    ensureSaberDefaults();
    std::unique_ptr<LeakChecker> saber = makeChecker(checker);
    saber->runOnModule(pag);

    std::vector<json> bugs;
    for (const GenericBug* bug : saber->getBugReport().getBugSet())
        bugs.push_back(bugRecord(bug));
    std::sort(bugs.begin(), bugs.end(), [](const json& a, const json& b)
    {
        const std::string at = a.value("type", "");
        const std::string bt = b.value("type", "");
        if (at != bt)
            return at < bt;
        const json& al = a["loc"];
        const json& bl = b["loc"];
        const std::string af = al.value("file", "");
        const std::string bf = bl.value("file", "");
        if (af != bf)
            return af < bf;
        const int aln = al.value("line", 0);
        const int bln = bl.value("line", 0);
        if (aln != bln)
            return aln < bln;
        return a.value("function", "") < b.value("function", "");
    });

    const size_t total = bugs.size();
    const bool truncated = total > kBugCap;
    if (truncated)
        bugs.resize(kBugCap);
    json outBugs = json::array();
    for (json& bug : bugs)
        outBugs.push_back(std::move(bug));
    return json{{"checker", checker},
                {"bugs", std::move(outBugs)},
                {"total", total},
                {"truncated", truncated},
                {"sources", saber->getSources().size()},
                {"sinks", saber->getSinks().size()}};
}

json QueryEngine::saberLeaks(const json&) const
{
    if (!saberLeaksReady)
    {
        saberLeaksCache = runSaberChecker("leak");
        saberLeaksReady = true;
    }
    return saberLeaksCache;
}

json QueryEngine::saberDoubleFrees(const json&) const
{
    if (!saberDoubleFreesReady)
    {
        saberDoubleFreesCache = runSaberChecker("double-free");
        saberDoubleFreesReady = true;
    }
    return saberDoubleFreesCache;
}

json QueryEngine::saberFileLeaks(const json&) const
{
    if (!saberFileLeaksReady)
    {
        saberFileLeaksCache = runSaberChecker("file");
        saberFileLeaksReady = true;
    }
    return saberFileLeaksCache;
}
