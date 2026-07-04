//===- GraphQueries.cpp -- generic graph browsing queries ----------------===//
#include "QueryEngine.h"
#include "Evidence.h"
#include "Graphs/CallGraph.h"
#include "Graphs/ICFG.h"
#include "Graphs/ICFGEdge.h"
#include "Graphs/SVFG.h"
#include "Graphs/SVFGEdge.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFStatements.h"
#include "SVFIR/SVFVariables.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace SVF;
using json = nlohmann::json;

namespace
{
constexpr size_t kDefaultLimit = 100;
constexpr size_t kMaxLimit = 1000;

enum class GraphKind
{
    ICFG,
    SVFG,
    SVFIR,
    CallGraph,
};

struct Page
{
    size_t offset;
    size_t limit;
};

struct EdgeRow
{
    NodeID src;
    NodeID dst;
    std::string kind;
    json extra = json::object();
};

GraphKind parseGraph(const json& params)
{
    if (!params.contains("graph") || !params["graph"].is_string())
        throw std::runtime_error(
            "missing required string param: graph (accepted: icfg, svfg, svfir, callgraph)");
    const std::string g = params["graph"].get<std::string>();
    if (g == "icfg")
        return GraphKind::ICFG;
    if (g == "svfg")
        return GraphKind::SVFG;
    if (g == "svfir")
        return GraphKind::SVFIR;
    if (g == "callgraph")
        return GraphKind::CallGraph;
    throw std::runtime_error("unknown graph '" + g +
                             "' (accepted: icfg, svfg, svfir, callgraph)");
}

const char* graphName(GraphKind g)
{
    switch (g)
    {
    case GraphKind::ICFG:
        return "icfg";
    case GraphKind::SVFG:
        return "svfg";
    case GraphKind::SVFIR:
        return "svfir";
    case GraphKind::CallGraph:
        return "callgraph";
    }
    return "unknown";
}

Page pageParams(const json& params)
{
    Page p{params.value("offset", 0U), params.value("limit", kDefaultLimit)};
    if (p.limit < 1 || p.limit > kMaxLimit)
        throw std::runtime_error("limit must be in [1, 1000]");
    return p;
}

bool matchKind(const json& node, const std::string& kind)
{
    return kind.empty() || node.value("kind", "") == kind;
}

bool matchFunc(const json& node, const std::string& func)
{
    if (func.empty())
        return true;
    const json& loc = node.value("loc", json::object());
    return loc.is_object() && loc.value("func", "") == func;
}

json pageNodes(const char* graph, std::vector<json> nodes, const Page& p)
{
    std::sort(nodes.begin(), nodes.end(), [](const json& a, const json& b)
    {
        return a.value("id", 0U) < b.value("id", 0U);
    });
    const size_t total = nodes.size();
    json out = json::array();
    for (size_t i = p.offset; i < nodes.size() && out.size() < p.limit; ++i)
        out.push_back(std::move(nodes[i]));
    return json{{"graph", graph},
                {"nodes", std::move(out)},
                {"total", total},
                {"offset", p.offset},
                {"limit", p.limit},
                {"truncated", p.offset + p.limit < total}};
}

json edgeJson(const EdgeRow& r)
{
    json e = {{"src", r.src}, {"dst", r.dst}, {"kind", r.kind}};
    for (auto it = r.extra.begin(); it != r.extra.end(); ++it)
        e[it.key()] = it.value();
    return e;
}

json pageEdges(const char* graph, std::vector<EdgeRow> edges, const Page& p)
{
    std::sort(edges.begin(), edges.end(), [](const EdgeRow& a, const EdgeRow& b)
    {
        if (a.src != b.src)
            return a.src < b.src;
        if (a.dst != b.dst)
            return a.dst < b.dst;
        return a.kind < b.kind;
    });
    const size_t total = edges.size();
    json out = json::array();
    for (size_t i = p.offset; i < edges.size() && out.size() < p.limit; ++i)
        out.push_back(edgeJson(edges[i]));
    return json{{"graph", graph},
                {"edges", std::move(out)},
                {"total", total},
                {"offset", p.offset},
                {"limit", p.limit},
                {"truncated", p.offset + p.limit < total}};
}

const char* stmtKindName(const SVFStmt* s)
{
    switch (s->getEdgeKind())
    {
    case SVFStmt::Addr:
        return "Addr";
    case SVFStmt::Copy:
        return "Copy";
    case SVFStmt::Store:
        return "Store";
    case SVFStmt::Load:
        return "Load";
    case SVFStmt::Call:
        return "Call";
    case SVFStmt::Ret:
        return "Ret";
    case SVFStmt::Gep:
        return "Gep";
    case SVFStmt::Phi:
        return "Phi";
    case SVFStmt::Select:
        return "Select";
    case SVFStmt::Cmp:
        return "Cmp";
    case SVFStmt::BinaryOp:
        return "BinaryOp";
    case SVFStmt::UnaryOp:
        return "UnaryOp";
    case SVFStmt::Branch:
        return "Branch";
    case SVFStmt::ThreadFork:
        return "ThreadFork";
    case SVFStmt::ThreadJoin:
        return "ThreadJoin";
    default:
        return "SVFStmt";
    }
}

const std::vector<SVFStmt::PEDGEK>& stmtKinds()
{
    static const std::vector<SVFStmt::PEDGEK> kinds = {
        SVFStmt::Addr,       SVFStmt::Copy,     SVFStmt::Store,
        SVFStmt::Load,       SVFStmt::Call,     SVFStmt::Ret,
        SVFStmt::Gep,        SVFStmt::Phi,      SVFStmt::Select,
        SVFStmt::Cmp,        SVFStmt::BinaryOp, SVFStmt::UnaryOp,
        SVFStmt::Branch,     SVFStmt::ThreadFork,
        SVFStmt::ThreadJoin,
    };
    return kinds;
}

std::string icfgEdgeKind(const ICFGEdge* e)
{
    if (e->isIntraCFGEdge())
        return "IntraCFGEdge";
    if (e->isCallCFGEdge())
        return "CallCFGEdge";
    if (e->isRetCFGEdge())
        return "RetCFGEdge";
    return "ICFGEdge";
}

std::string svfgEdgeKind(const VFGEdge* e)
{
    switch (e->getEdgeKind())
    {
    case VFGEdge::IntraDirectVF:
        return "IntraDirSVFGEdge";
    case VFGEdge::IntraIndirectVF:
        return "IntraIndSVFGEdge";
    case VFGEdge::CallDirVF:
        return "CallDirSVFGEdge";
    case VFGEdge::RetDirVF:
        return "RetDirSVFGEdge";
    case VFGEdge::CallIndVF:
        return "CallIndSVFGEdge";
    case VFGEdge::RetIndVF:
        return "RetIndSVFGEdge";
    case VFGEdge::TheadMHPIndirectVF:
        return "ThreadMHPIndSVFGEdge";
    default:
        return "VFGEdge";
    }
}

json callGraphNodeRecord(const CallGraphNode* n)
{
    const FunObjVar* f = n->getFunction();
    return json{{"kind", "CallGraphNode"},
                {"id", n->getId()},
                {"function", f ? f->getName() : ""},
                {"loc", f ? evidence::loc(f->getSourceLoc()) : json::object()},
                {"is_decl", f ? f->isDeclaration() : false},
                {"num_args", f ? f->arg_size() : 0}};
}

void addCallGraphEdgeRows(const CallGraphEdge* e, std::vector<EdgeRow>& rows)
{
    for (auto it = e->directCallsBegin(); it != e->directCallsEnd(); ++it)
        rows.push_back({e->getSrcID(), e->getDstID(), "DirectCallGraphEdge",
                        {{"direct", true}, {"callsite", evidence::node(*it)}}});
    for (auto it = e->indirectCallsBegin(); it != e->indirectCallsEnd(); ++it)
        rows.push_back({e->getSrcID(), e->getDstID(), "IndirectCallGraphEdge",
                        {{"direct", false}, {"callsite", evidence::node(*it)}}});
}

std::vector<EdgeRow> filterEdges(std::vector<EdgeRow> rows,
                                 const std::string& kind)
{
    if (kind.empty())
        return rows;
    std::vector<EdgeRow> out;
    for (EdgeRow& r : rows)
        if (r.kind == kind)
            out.push_back(std::move(r));
    return out;
}

} // namespace

json QueryEngine::graphs(const json&) const
{
    auto countICFGEdges = [&]()
    {
        size_t n = 0;
        for (const auto& it : *pag->getICFG())
            n += it.second->getOutEdges().size();
        return n;
    };
    auto countSVFGEdges = [&]()
    {
        size_t n = 0;
        for (const auto& it : *svfg)
            n += it.second->getOutEdges().size();
        return n;
    };
    auto countSVFIREdges = [&]()
    {
        size_t n = 0;
        for (const auto& it : *pag)
            for (SVFStmt::PEDGEK k : stmtKinds())
                if (it.second->hasOutgoingEdges(k))
                    n += std::distance(it.second->getOutgoingEdgesBegin(k),
                                       it.second->getOutgoingEdgesEnd(k));
        return n;
    };
    auto countCallGraphEdges = [&]()
    {
        size_t n = 0;
        for (const auto& it : *callgraph)
            for (const CallGraphEdge* e : it.second->getOutEdges())
            {
                n += std::distance(e->directCallsBegin(), e->directCallsEnd());
                n += std::distance(e->indirectCallsBegin(), e->indirectCallsEnd());
            }
        return n;
    };
    return json{{"graphs", json::array({
                    {{"name", "icfg"},
                     {"nodes", pag->getICFG()->getTotalNodeNum()},
                     {"edges", countICFGEdges()}},
                    {{"name", "svfg"},
                     {"nodes", svfg->getTotalNodeNum()},
                     {"edges", countSVFGEdges()}},
                    {{"name", "svfir"},
                     {"nodes", pag->getTotalNodeNum()},
                     {"edges", countSVFIREdges()}},
                    {{"name", "callgraph"},
                     {"nodes", callgraph->getTotalNodeNum()},
                     {"edges", countCallGraphEdges()}},
                })}};
}

json QueryEngine::graphNodes(const json& params) const
{
    const GraphKind g = parseGraph(params);
    const Page p = pageParams(params);
    const std::string kind = params.value("kind", "");
    const std::string func = params.value("func", "");
    std::vector<json> nodes;

    switch (g)
    {
    case GraphKind::ICFG:
        for (const auto& it : *pag->getICFG())
        {
            json n = evidence::node(it.second);
            if (matchKind(n, kind) && matchFunc(n, func))
                nodes.push_back(std::move(n));
        }
        break;
    case GraphKind::SVFG:
        for (const auto& it : *svfg)
        {
            json n = evidence::node(it.second);
            if (matchKind(n, kind) && matchFunc(n, func))
                nodes.push_back(std::move(n));
        }
        break;
    case GraphKind::SVFIR:
        for (const auto& it : *pag)
        {
            json n = evidence::node(it.second);
            if (matchKind(n, kind) && matchFunc(n, func))
                nodes.push_back(std::move(n));
        }
        break;
    case GraphKind::CallGraph:
        for (const auto& it : *callgraph)
        {
            json n = callGraphNodeRecord(it.second);
            if (matchKind(n, kind) &&
                (func.empty() || n.value("function", "") == func))
                nodes.push_back(std::move(n));
        }
        break;
    }
    return pageNodes(graphName(g), std::move(nodes), p);
}

json QueryEngine::graphEdges(const json& params) const
{
    const GraphKind g = parseGraph(params);
    const Page p = pageParams(params);
    const std::string kind = params.value("kind", "");
    std::vector<EdgeRow> rows;

    switch (g)
    {
    case GraphKind::ICFG:
        for (const auto& it : *pag->getICFG())
            for (const ICFGEdge* e : it.second->getOutEdges())
                rows.push_back({e->getSrcID(), e->getDstID(), icfgEdgeKind(e)});
        break;
    case GraphKind::SVFG:
        for (const auto& it : *svfg)
            for (const VFGEdge* e : it.second->getOutEdges())
                rows.push_back({e->getSrcID(), e->getDstID(), svfgEdgeKind(e)});
        break;
    case GraphKind::SVFIR:
        for (const auto& it : *pag)
            for (SVFStmt::PEDGEK k : stmtKinds())
                if (it.second->hasOutgoingEdges(k))
                    for (auto ei = it.second->getOutgoingEdgesBegin(k);
                         ei != it.second->getOutgoingEdgesEnd(k); ++ei)
                        rows.push_back({(*ei)->getSrcID(), (*ei)->getDstID(),
                                        stmtKindName(*ei),
                                        {{"stmt_id", (*ei)->getEdgeID()}}});
        break;
    case GraphKind::CallGraph:
        for (const auto& it : *callgraph)
            for (const CallGraphEdge* e : it.second->getOutEdges())
                addCallGraphEdgeRows(e, rows);
        break;
    }
    return pageEdges(graphName(g), filterEdges(std::move(rows), kind), p);
}

json QueryEngine::nodeQ(const json& params) const
{
    const GraphKind g = parseGraph(params);
    if (!params.contains("id") || !params["id"].is_number_unsigned())
        throw std::runtime_error("missing required unsigned integer param: id");
    const NodeID id = params["id"].get<NodeID>();

    switch (g)
    {
    case GraphKind::ICFG:
        if (!pag->getICFG()->hasGNode(id))
            throw std::runtime_error("icfg node not found: " + std::to_string(id));
        return json{{"graph", "icfg"},
                    {"node", evidence::node(pag->getICFG()->getGNode(id))}};
    case GraphKind::SVFG:
        if (!svfg->hasGNode(id))
            throw std::runtime_error("svfg node not found: " + std::to_string(id));
        return json{{"graph", "svfg"}, {"node", evidence::node(svfg->getGNode(id))}};
    case GraphKind::SVFIR:
        if (!pag->hasGNode(id))
            throw std::runtime_error("svfir node not found: " + std::to_string(id));
        return json{{"graph", "svfir"}, {"node", evidence::node(pag->getGNode(id))}};
    case GraphKind::CallGraph:
        if (!callgraph->hasGNode(id))
            throw std::runtime_error("callgraph node not found: " +
                                     std::to_string(id));
        return json{{"graph", "callgraph"},
                    {"node", callGraphNodeRecord(callgraph->getGNode(id))}};
    }
    throw std::runtime_error("unsupported graph");
}

json QueryEngine::neighbors(const json& params) const
{
    const GraphKind g = parseGraph(params);
    if (!params.contains("id") || !params["id"].is_number_unsigned())
        throw std::runtime_error("missing required unsigned integer param: id");
    const NodeID id = params["id"].get<NodeID>();
    const std::string direction = params.value("direction", "both");
    if (direction != "in" && direction != "out" && direction != "both")
        throw std::runtime_error("direction must be one of: in, out, both");

    json n = nodeQ(params)["node"];
    std::vector<EdgeRow> inRows, outRows;

    switch (g)
    {
    case GraphKind::ICFG:
    {
        const ICFGNode* node = pag->getICFG()->getGNode(id);
        for (const ICFGEdge* e : node->getInEdges())
            inRows.push_back({e->getSrcID(), e->getDstID(), icfgEdgeKind(e)});
        for (const ICFGEdge* e : node->getOutEdges())
            outRows.push_back({e->getSrcID(), e->getDstID(), icfgEdgeKind(e)});
        break;
    }
    case GraphKind::SVFG:
    {
        const VFGNode* node = svfg->getGNode(id);
        for (const VFGEdge* e : node->getInEdges())
            inRows.push_back({e->getSrcID(), e->getDstID(), svfgEdgeKind(e)});
        for (const VFGEdge* e : node->getOutEdges())
            outRows.push_back({e->getSrcID(), e->getDstID(), svfgEdgeKind(e)});
        break;
    }
    case GraphKind::SVFIR:
    {
        const SVFVar* node = pag->getGNode(id);
        for (SVFStmt::PEDGEK k : stmtKinds())
        {
            if (node->hasIncomingEdges(k))
                for (auto ei = node->getIncomingEdgesBegin(k);
                     ei != node->getIncomingEdgesEnd(k); ++ei)
                    inRows.push_back({(*ei)->getSrcID(), (*ei)->getDstID(),
                                      stmtKindName(*ei),
                                      {{"stmt_id", (*ei)->getEdgeID()}}});
            if (node->hasOutgoingEdges(k))
                for (auto ei = node->getOutgoingEdgesBegin(k);
                     ei != node->getOutgoingEdgesEnd(k); ++ei)
                    outRows.push_back({(*ei)->getSrcID(), (*ei)->getDstID(),
                                       stmtKindName(*ei),
                                       {{"stmt_id", (*ei)->getEdgeID()}}});
        }
        break;
    }
    case GraphKind::CallGraph:
    {
        const CallGraphNode* node = callgraph->getGNode(id);
        for (const CallGraphEdge* e : node->getInEdges())
            addCallGraphEdgeRows(e, inRows);
        for (const CallGraphEdge* e : node->getOutEdges())
            addCallGraphEdgeRows(e, outRows);
        break;
    }
    }

    json out = {{"graph", graphName(g)}, {"id", id}, {"node", std::move(n)}};
    if (direction == "in" || direction == "both")
        out["in_edges"] = pageEdges(graphName(g), std::move(inRows),
                                    Page{0, kMaxLimit})["edges"];
    if (direction == "out" || direction == "both")
        out["out_edges"] = pageEdges(graphName(g), std::move(outRows),
                                     Page{0, kMaxLimit})["edges"];
    return out;
}
