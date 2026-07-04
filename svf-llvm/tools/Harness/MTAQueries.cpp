//===- MTAQueries.cpp -- QueryEngine MTA thread/MHP summaries ------------===//
//
// SVF's MTA implementation is primarily a command-line analysis and writes
// graph dumps/progress text while running. This adapter keeps the harness'
// JSON contract stable by isolating those side effects, then exposes only
// public MTA/MHP/TCT/ThreadCallGraph data.
//
//===----------------------------------------------------------------------===//
#include "QueryEngine.h"
#include "Evidence.h"
#include "Graphs/ICFG.h"
#include "Graphs/ICFGNode.h"
#include "Graphs/ThreadCallGraph.h"
#include "MTA/MHP.h"
#include "MTA/MTA.h"
#include "MTA/TCT.h"
#include "SVFIR/SVFIR.h"
#include "Util/Options.h"
#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <limits.h>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using namespace SVF;
using json = nlohmann::json;

namespace
{
constexpr size_t kSiteCap = 200;
constexpr size_t kThreadCap = 200;
constexpr size_t kAnchorNodeCap = 50;
constexpr size_t kWitnessCap = 20;

void ensureMTADefaults()
{
    const_cast<Option<bool>&>(Options::PStat).setValue(false);
    const_cast<Option<bool>&>(Options::AllPairMHP).setValue(false);
    const_cast<Option<bool>&>(Options::RaceCheck).setValue(false);
    const_cast<Option<bool>&>(Options::EnableThreadCallGraph).setValue(true);
}

class ScopedStdoutSilencer
{
public:
    ScopedStdoutSilencer()
    {
        std::fflush(stdout);
        saved = dup(STDOUT_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (saved >= 0 && devNull >= 0)
            dup2(devNull, STDOUT_FILENO);
        if (devNull >= 0)
            close(devNull);
    }

    ~ScopedStdoutSilencer()
    {
        std::fflush(stdout);
        if (saved >= 0)
        {
            dup2(saved, STDOUT_FILENO);
            close(saved);
        }
    }

private:
    int saved = -1;
};

class ScopedTempCwd
{
public:
    ScopedTempCwd()
    {
        char cwdBuf[PATH_MAX];
        if (!getcwd(cwdBuf, sizeof(cwdBuf)))
            throw std::runtime_error("cannot capture current working directory");
        oldCwd = cwdBuf;

        char tmpl[] = "/tmp/svf-harness-mta-XXXXXX";
        char* made = mkdtemp(tmpl);
        if (!made)
            throw std::runtime_error("cannot create temporary MTA directory");
        tmp = made;
        if (chdir(tmp.c_str()) != 0)
            throw std::runtime_error("cannot enter temporary MTA directory");
    }

    ~ScopedTempCwd()
    {
        if (!oldCwd.empty())
        {
            if (chdir(oldCwd.c_str()) != 0)
            {
            }
        }
        if (!tmp.empty())
            std::filesystem::remove_all(tmp);
    }

private:
    std::string oldCwd;
    std::string tmp;
};

json siteRecord(const CallICFGNode* site, const json& endpoints,
                const char* endpointKey)
{
    return json{{"callsite", evidence::node(site)}, {endpointKey, endpoints}};
}

std::vector<json> sortedJsonByCallsite(std::vector<json> rows)
{
    std::sort(rows.begin(), rows.end(), [](const json& a, const json& b)
    {
        return a["callsite"].value("id", 0U) < b["callsite"].value("id", 0U);
    });
    return rows;
}

json stringArraySorted(std::vector<std::string> names)
{
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    json out = json::array();
    for (const std::string& name : names)
        out.push_back(name);
    return out;
}

struct SiteRows
{
    std::vector<json> rows;
    size_t edgeCount = 0;
};

SiteRows forkRows(const ThreadCallGraph* tcg)
{
    SiteRows out;
    for (auto it = tcg->forksitesBegin(), end = tcg->forksitesEnd();
         it != end; ++it)
    {
        std::vector<std::string> targets;
        if (tcg->hasThreadForkEdge(*it))
        {
            for (auto eit = tcg->getForkEdgeBegin(*it),
                      eend = tcg->getForkEdgeEnd(*it);
                 eit != eend; ++eit)
            {
                ++out.edgeCount;
                const FunObjVar* fun = (*eit)->getDstNode()->getFunction();
                if (fun)
                    targets.push_back(fun->getName());
            }
        }
        out.rows.push_back(siteRecord(*it, stringArraySorted(targets),
                                      "targets"));
    }
    return out;
}

SiteRows joinRows(const ThreadCallGraph* tcg)
{
    SiteRows out;
    for (auto it = tcg->joinsitesBegin(), end = tcg->joinsitesEnd();
         it != end; ++it)
    {
        std::vector<std::string> routines;
        if (tcg->hasThreadJoinEdge(*it))
        {
            for (auto eit = tcg->getJoinEdgeBegin(*it),
                      eend = tcg->getJoinEdgeEnd(*it);
                 eit != eend; ++eit)
            {
                ++out.edgeCount;
                const FunObjVar* fun = (*eit)->getDstNode()->getFunction();
                if (fun)
                    routines.push_back(fun->getName());
            }
        }
        out.rows.push_back(siteRecord(*it, stringArraySorted(routines),
                                      "routines"));
    }
    return out;
}

json tctThreadRecord(const TCTNode* node)
{
    const CxtThread& ctx = node->getCxtThread();
    const ICFGNode* fork = ctx.getThread();
    return json{{"id", node->getId()},
                {"forksite", fork ? evidence::node(fork) : json()},
                {"context", ctx.cxtToStr()},
                {"in_loop", node->isInloop()},
                {"in_cycle", node->isIncycle()},
                {"multi_forked", node->isMultiforked()}};
}

json cappedArray(std::vector<json> rows, size_t cap)
{
    if (rows.size() > cap)
        rows.resize(cap);
    json out = json::array();
    for (json& row : rows)
        out.push_back(std::move(row));
    return out;
}

const json& requiredObject(const json& params, const char* key)
{
    if (!params.contains(key) || !params[key].is_object())
        throw std::runtime_error(std::string("missing required object param: ") +
                                 key);
    return params[key];
}

std::vector<const ICFGNode*> resolveICFGLineAnchor(SVFIR* pag,
                                                   const json& spec)
{
    if (!spec.contains("file") || !spec["file"].is_string() ||
        !spec.contains("line") || !spec["line"].is_number_integer())
        throw std::runtime_error(
            "MTA source-location anchor needs {file: string, line: integer, kind?: string}");
    const std::string file = spec["file"].get<std::string>();
    const int line = spec["line"].get<int>();
    const std::string kind = spec.value("kind", "");

    std::vector<const ICFGNode*> nodes;
    std::set<int> nearby;
    for (const auto& it : *pag->getICFG())
    {
        const ICFGNode* node = it.second;
        json rec = evidence::node(node);
        const json& loc = rec["loc"];
        if (!evidence::fileMatches(loc.value("file", ""), file))
            continue;
        const int nodeLine = loc.value("line", 0);
        if (nodeLine > 0)
            nearby.insert(nodeLine);
        if (nodeLine != line)
            continue;
        if (!kind.empty() && rec.value("kind", "") != kind)
            continue;
        nodes.push_back(node);
    }
    std::sort(nodes.begin(), nodes.end(), [](const ICFGNode* a,
                                             const ICFGNode* b)
    {
        return a->getId() < b->getId();
    });
    if (nodes.empty())
    {
        std::string msg = "no ICFG nodes at " + file + ":" +
                          std::to_string(line);
        if (!nearby.empty())
        {
            msg += "; nearby lines:";
            size_t n = 0;
            for (int l : nearby)
            {
                if (n++ == 5)
                    break;
                msg += " " + std::to_string(l);
            }
        }
        throw std::runtime_error(msg);
    }
    return nodes;
}
} // namespace

MTA* QueryEngine::getMTA() const
{
    if (!mta)
    {
        ensureMTADefaults();
        mta = std::make_unique<MTA>();
        ScopedTempCwd cwd;
        ScopedStdoutSilencer silence;
        mta->runOnModule(pag);
    }
    return mta.get();
}

json QueryEngine::mtaSummary(const json&) const
{
    MHP* mhp = getMTA()->getMHP();
    ThreadCallGraph* tcg = mhp->getThreadCallGraph();
    TCT* tct = mhp->getTCT();

    SiteRows forks = forkRows(tcg);
    SiteRows joins = joinRows(tcg);
    forks.rows = sortedJsonByCallsite(std::move(forks.rows));
    joins.rows = sortedJsonByCallsite(std::move(joins.rows));

    std::vector<json> threads;
    for (const auto& it : *tct)
        threads.push_back(tctThreadRecord(it.second));
    std::sort(threads.begin(), threads.end(), [](const json& a, const json& b)
    {
        return a.value("id", 0U) < b.value("id", 0U);
    });

    const bool truncated = forks.rows.size() > kSiteCap ||
                           joins.rows.size() > kSiteCap ||
                           threads.size() > kThreadCap;
    return json{{"analysis", "mta"},
                {"threads", tct->getTCTNodeNum()},
                {"tct_edges", tct->getTCTEdgeNum()},
                {"max_context", tct->getMaxCxtSize()},
                {"candidate_functions", tct->getMakredProcs().size()},
                {"entry_functions", tct->getEntryProcs().size()},
                {"fork_sites", tcg->getNumOfForksite()},
                {"join_sites", tcg->getNumOfJoinsite()},
                {"par_for_sites", tcg->getNumOfParForSite()},
                {"fork_edges", forks.edgeCount},
                {"join_edges", joins.edgeCount},
                {"mhp_queries", mhp->numOfTotalQueries},
                {"mhp_pairs", mhp->numOfMHPQueries},
                {"forks", cappedArray(std::move(forks.rows), kSiteCap)},
                {"joins", cappedArray(std::move(joins.rows), kSiteCap)},
                {"thread_records", cappedArray(std::move(threads), kThreadCap)},
                {"truncated", truncated}};
}

json QueryEngine::mtaMHP(const json& params) const
{
    MHP* mhp = getMTA()->getMHP();
    std::vector<const ICFGNode*> left =
        resolveICFGLineAnchor(pag, requiredObject(params, "left"));
    std::vector<const ICFGNode*> right =
        resolveICFGLineAnchor(pag, requiredObject(params, "right"));

    const size_t leftTotal = left.size();
    const size_t rightTotal = right.size();
    bool truncated = left.size() > kAnchorNodeCap ||
                     right.size() > kAnchorNodeCap;
    if (left.size() > kAnchorNodeCap)
        left.resize(kAnchorNodeCap);
    if (right.size() > kAnchorNodeCap)
        right.resize(kAnchorNodeCap);

    bool any = false;
    size_t checked = 0;
    json witnesses = json::array();
    for (const ICFGNode* l : left)
    {
        for (const ICFGNode* r : right)
        {
            ++checked;
            const bool mhpPair = mhp->mayHappenInParallelInst(l, r);
            if (!mhpPair)
                continue;
            any = true;
            if (witnesses.size() < kWitnessCap)
            {
                witnesses.push_back({{"left", evidence::node(l)},
                                     {"right", evidence::node(r)},
                                     {"same_thread",
                                      mhp->executedByTheSameThread(l, r)}});
            }
            else
            {
                truncated = true;
            }
        }
    }

    return json{{"analysis", "mta"},
                {"left_matches", leftTotal},
                {"right_matches", rightTotal},
                {"pairs_checked", checked},
                {"may_happen_in_parallel", any},
                {"witnesses", std::move(witnesses)},
                {"truncated", truncated}};
}
