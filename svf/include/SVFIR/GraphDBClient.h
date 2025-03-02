#ifndef INCLUDE_GRAPHDBCLIENT_H_
#define INCLUDE_GRAPHDBCLIENT_H_
#include "Graphs/CallGraph.h"
#include "Graphs/ICFGEdge.h"
#include "Graphs/ICFGNode.h"
#include "Util/SVFUtil.h"
#include "lgraph/lgraph_rpc_client.h"
#include <errno.h>
#include <stdio.h>

namespace SVF
{
class RpcClient;
class ICFGEdge;
class ICFGNode;
class CallGraphEdge;
class CallGraphNode;
class GraphDBClient
{
private:
    lgraph::RpcClient* connection;

    GraphDBClient()
    {
        const char* url = "127.0.0.1:9090";
        connection = new lgraph::RpcClient(url, "admin", "73@TuGraph");
    }

    ~GraphDBClient()
    {
        if (connection != nullptr)
        {
            connection = nullptr;
        }
    }

public:
    static GraphDBClient& getInstance()
    {
        static GraphDBClient instance;
        return instance;
    }

    GraphDBClient(const GraphDBClient&) = delete;
    GraphDBClient& operator=(const GraphDBClient&) = delete;

    lgraph::RpcClient* getConnection()
    {
        return connection;
    }

    bool loadSchema(lgraph::RpcClient* connection, const std::string& filepath,
                    const std::string& dbname);
    bool createSubGraph(lgraph::RpcClient* connection, const std::string& graphname);
    bool addCallGraphNode2db(lgraph::RpcClient* connection,
                             const CallGraphNode* node,
                             const std::string& dbname);
    bool addCallGraphEdge2db(lgraph::RpcClient* connection,
                             const CallGraphEdge* edge, const std::string& dbname);
    bool addICFGNode2db(lgraph::RpcClient* connection, const ICFGNode* node,
                        const std::string& dbname);
    bool addICFGEdge2db(lgraph::RpcClient* connection, const ICFGEdge* edge,
                        const std::string& dbname);
    /// pasre the directcallsIds/indirectcallsIds string to vector
    std::vector<int> stringToIds(const std::string& str);

    template <typename Container>
    std::string extractNodesIds(const Container& nodes)
    {
        if (nodes.empty())
        {
            return "";
        }
        std::ostringstream nodesIds;
        auto it = nodes.begin();

        nodesIds << (*it)->getId();
        ++it;

        for (; it != nodes.end(); ++it)
        {
            nodesIds << "," << (*it)->getId();
        }

        return nodesIds.str();
    }

    template <typename Container>
    std::string extractEdgesIds(const Container& edges)
    {
        if (edges.empty())
        {
            return "";
        }
        std::ostringstream edgesIds;
        auto it = edges.begin();

        edgesIds << (*it)->getEdgeID();
        ++it;

        for (; it != edges.end(); ++it)
        {
            edgesIds << "," << (*it)->getEdgeID();
        }

        return edgesIds.str();
    }

    /// parse and extract the directcallsIds/indirectcallsIds vector

    /// parse ICFGNodes & generate the insert statement for ICFGNodes
    std::string getGlobalICFGNodeInsertStmt(const GlobalICFGNode* node);

    std::string getIntraICFGNodeInsertStmt(const IntraICFGNode* node);

    std::string getInterICFGNodeInsertStmt(const InterICFGNode* node);

    std::string getFunExitICFGNodeInsertStmt(const FunExitICFGNode* node);

    std::string getFunEntryICFGNodeInsertStmt(const FunEntryICFGNode* node);

    std::string getCallICFGNodeInsertStmt(const CallICFGNode* node);

    std::string getRetICFGNodeInsertStmt(const RetICFGNode* node);

    std::string getIntraCFGEdgeStmt(const IntraCFGEdge* edge);

    std::string getCallCFGEdgeStmt(const CallCFGEdge* edge);

    std::string getRetCFGEdgeStmt(const RetCFGEdge* edge);

    std::string getICFGNodeKindString(const ICFGNode* node);

    void insertICFG2db(const ICFG* icfg);

    void insertCallGraph2db(const CallGraph* callGraph);
};

} // namespace SVF

#endif