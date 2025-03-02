#include "SVFIR/GraphDBClient.h"
#include "SVFIR/SVFVariables.h"

using namespace SVF;

bool GraphDBClient::loadSchema(lgraph::RpcClient* connection,
                               const std::string& filepath,
                               const std::string& dbname)
{
    if (nullptr != connection)
    {
        SVFUtil::outs() << "load schema from file:" << filepath << "\n";
        std::string result;
        bool ret =
            connection->ImportSchemaFromFile(result, filepath, dbname);
        if (ret)
        {
            SVFUtil::outs() << dbname<< "schema load successfully:" << result << "\n";
        }
        else
        {
            SVFUtil::outs() << dbname<< "schema load failed:" << result << "\n";
        }
        return ret;
    }
    return false;
}

// create a new graph name CallGraph in db
bool GraphDBClient::createSubGraph(lgraph::RpcClient* connection, const std::string& graphname)
{
     ///TODO: graph name should be configurable
    if (nullptr != connection)
    {
        std::string result;
        bool ret = connection->CallCypherToLeader(
            result, "CALL dbms.graph.createGraph('"+graphname+"')");
        if (ret)
        {
            SVFUtil::outs()
                << "Create Graph callGraph successfully:" << result << "\n";
        }
        else
        {
            SVFUtil::outs()
                << "Failed to create Graph callGraph:" << result << "\n";
        }
    }
    return false;
}

bool GraphDBClient::addICFGEdge2db(lgraph::RpcClient* connection,
                                   const ICFGEdge* edge,
                                   const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string queryStatement;
        if(SVFUtil::isa<IntraCFGEdge>(edge))
        {
            queryStatement = getIntraCFGEdgeStmt(SVFUtil::cast<IntraCFGEdge>(edge));
        }
        else if (SVFUtil::isa<CallCFGEdge>(edge))
        {
            queryStatement = getCallCFGEdgeStmt(SVFUtil::cast<CallCFGEdge>(edge));
        }
        else if (SVFUtil::isa<RetCFGEdge>(edge))
        {
            queryStatement = getRetCFGEdgeStmt(SVFUtil::cast<RetCFGEdge>(edge));
        }
        else 
        {
            assert("unknown icfg edge type?");
            return false;
        }
        SVFUtil::outs() << "query:" << queryStatement << "\n";
        std::string result;
        if (queryStatement.empty())
        {
            return false;
        }
        bool ret = connection->CallCypher(result, queryStatement, dbname);
        if (ret)
        {
            SVFUtil::outs() << "ICFG edge added: " << result << "\n";
        }
        else
        {
            SVFUtil::outs() << "Failed to add ICFG edge to db " << dbname << " "
                            << result << "\n";
        }
        return ret;

    }
    return false;
}

bool GraphDBClient::addICFGNode2db(lgraph::RpcClient* connection,
                                   const ICFGNode* node,
                                   const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string queryStatement;
        if(SVFUtil::isa<GlobalICFGNode>(node))
        {
           queryStatement = getGlobalICFGNodeInsertStmt(SVFUtil::cast<GlobalICFGNode>(node)); 
        }
        else if (SVFUtil::isa<IntraICFGNode>(node))
        {
            queryStatement = getIntraICFGNodeInsertStmt(SVFUtil::cast<IntraICFGNode>(node));
        }
        else if (SVFUtil::isa<FunEntryICFGNode>(node))
        {
            queryStatement = getFunEntryICFGNodeInsertStmt(SVFUtil::cast<FunEntryICFGNode>(node));
        }
        else if (SVFUtil::isa<FunExitICFGNode>(node))
        {
            queryStatement = getFunExitICFGNodeInsertStmt(SVFUtil::cast<FunExitICFGNode>(node));
        }
        else if (SVFUtil::isa<CallICFGNode>(node))
        {
            queryStatement = getCallICFGNodeInsertStmt(SVFUtil::cast<CallICFGNode>(node));
        }
        else if (SVFUtil::isa<RetICFGNode>(node))
        {
            queryStatement = getRetICFGNodeInsertStmt(SVFUtil::cast<RetICFGNode>(node));
        }
        else 
        {
            assert("unknown icfg node type?");
            return false;
        }

        SVFUtil::outs()<<"query:"<<queryStatement<<"\n";
        std::string result;
        bool ret = connection->CallCypher(result, queryStatement, dbname);
        if (ret)
        {
            SVFUtil::outs()<< "ICFG node added: " << result << "\n";
        }
        else
        {
            SVFUtil::outs() << "Failed to add icfg node to db " << dbname << " "
                            << result << "\n";
        }
        return ret;
    }
    return false;
}

bool GraphDBClient::addCallGraphNode2db(lgraph::RpcClient* connection,
                                        const CallGraphNode* node,
                                        const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string is_reachable_str = node->isReachableFromProgEntry() ? "true" : "false";
        const std::string queryStatement ="CREATE (n:CallGraphNode {id: " + std::to_string(node->getId()) +
                                 ", fun_obj_var_id: " + std::to_string(node->getFunction()->getId()) +
                                 ", is_reachable_from_prog_entry: " + is_reachable_str + "})";
        SVFUtil::outs()<<"query:"<<queryStatement<<"\n";
        std::string result;
        bool ret = connection->CallCypher(result, queryStatement, dbname);
        if (ret)
        {
            SVFUtil::outs()<< "CallGraph node added: " << result << "\n";
        }
        else
        {
            SVFUtil::outs() << "Failed to add callGraph node to db " << dbname << " "
                            << result << "\n";
        }
        return ret;
    }
    return false;
}

bool GraphDBClient::addCallGraphEdge2db(lgraph::RpcClient* connection,
                                        const CallGraphEdge* edge,
                                        const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string indirectCallIds = "";
        Set<const CallICFGNode*> indirectCall = edge->getIndirectCalls();
        if (indirectCall.size() > 0)
        {
            indirectCallIds = extractNodesIds(indirectCall);
        }

        std::string directCallIds = "";
        Set<const CallICFGNode*> directCall = edge->getDirectCalls();
        if (directCall.size() > 0)
        {
            directCallIds = extractNodesIds(directCall);
        }

        const std::string queryStatement =
            "MATCH (n:CallGraphNode), (m:CallGraphNode) WHERE n.id = " +
            std::to_string(edge->getSrcNode()->getId()) +
            " AND m.id = " + std::to_string(edge->getDstNode()->getId()) +
            " CREATE (n)-[r:CallGraphEdge{csid:" + std::to_string(edge->getCallSiteID()) +
            ", kind:" + std::to_string(edge->getEdgeKind()) +
            ", direct_call_set:'" + directCallIds + "', indirect_call_set:'" +
            indirectCallIds + "'}]->(m)";
        SVFUtil::outs() << "query:" << queryStatement << "\n";
        std::string result;
        bool ret = connection->CallCypher(result, queryStatement, dbname);
        if (ret)
        {
            SVFUtil::outs() << "CallGraph edge added: " << result << "\n";
        }
        else
        {
            SVFUtil::outs() << "Failed to add callgraph edge to db " << dbname << " "
                            << result << "\n";
        }
        return ret;
    }
    return false;
}

// pasre the directcallsIds/indirectcallsIds string to vector
std::vector<int> GraphDBClient::stringToIds(const std::string& str)
{
    std::vector<int> ids;
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, ','))
    {
        ids.push_back(std::stoi(token));
    }
    return ids;
}

std::string GraphDBClient::getGlobalICFGNodeInsertStmt(const GlobalICFGNode* node) {
    const std::string queryStatement ="CREATE (n:GlobalICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) + "})";
    return queryStatement;
}

std::string GraphDBClient::getIntraICFGNodeInsertStmt(const IntraICFGNode* node) {
    const std::string queryStatement ="CREATE (n:IntraICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    ", is_return: " + (node->isRetInst() ? "true" : "false") +
    ", bb_id:" + std::to_string(node->getBB()->getId()) + "})";
    return queryStatement;
}

std::string GraphDBClient::getInterICFGNodeInsertStmt(const InterICFGNode* node) {
    const std::string queryStatement ="CREATE (n:InterICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) + "})";
    return queryStatement;
}

std::string GraphDBClient::getFunEntryICFGNodeInsertStmt(const FunEntryICFGNode* node) {
    const std::string queryStatement ="CREATE (n:FunEntryICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    ", fun_obj_var_id:" + std::to_string(node->getFun()->getId()) + 
    ", fp_nodes:'" + extractNodesIds(node->getFormalParms()) +"'})";
    return queryStatement;
}

std::string GraphDBClient::getFunExitICFGNodeInsertStmt(const FunExitICFGNode* node) {
    std::string formalRetId = "";
    if (node->getFormalRet() == nullptr)
    {
        formalRetId = "";
    } else {
        formalRetId = ",formal_ret_node_id:" + std::to_string(node->getFormalRet()->getId());
    }
    const std::string queryStatement ="CREATE (n:FunExitICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    ", fun_obj_var_id:" + std::to_string(node->getFun()->getId()) + 
    formalRetId + "})";
    return queryStatement;
}

std::string GraphDBClient::getCallICFGNodeInsertStmt(const CallICFGNode* node) {
    std::string fun_name_of_v_call = "";
    std::string vtab_ptr_node_id = "";
    std::string virtual_fun_idx = "0";
    if (node->isVirtualCall())
    {
        fun_name_of_v_call = ", fun_name_of_v_call: '"+node->getFunNameOfVirtualCall()+"'";
        vtab_ptr_node_id = ", vtab_ptr_node_id:" + std::to_string(node->getVtablePtr()->getId());
        virtual_fun_idx = ", virtual_fun_idx:" + std::to_string(node->getFunIdxInVtable());
    }
    const std::string queryStatement ="CREATE (n:CallICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    ", ret_icfg_node_id: " + std::to_string(node->getRetICFGNode()->getId()) +
    ", bb_id: " + std::to_string(node->getBB()->getId()) +
    ", svf_type: " + std::to_string(node->getType()->getKind()) +
    ", ap_nodes:'" + extractNodesIds(node->getActualParms()) +
    "', called_fun_obj_var_id:" + std::to_string(node->getCalledFunction()->getId()) +
    ", is_vararg: " + (node->isVarArg() ? "true" : "false") +
    ", is_vir_call_inst: " + (node->isVirtualCall() ? "true" : "false") +
    vtab_ptr_node_id+virtual_fun_idx+fun_name_of_v_call+"})";
    return queryStatement;
}

std::string GraphDBClient::getRetICFGNodeInsertStmt(const RetICFGNode* node) {
    std::string actual_ret_node_id="";
    if (node->getActualRet() != nullptr)
    {
        actual_ret_node_id = ", actual_ret_node_id: " + std::to_string(node->getActualRet()->getId()) ;
    }
    const std::string queryStatement ="CREATE (n:RetICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    actual_ret_node_id+
    ", call_block_node_id: " + std::to_string(node->getCallICFGNode()->getId()) + "})";
    return queryStatement;
}

std::string GraphDBClient::getICFGNodeKindString(const ICFGNode* node)
{
    if(SVFUtil::isa<GlobalICFGNode>(node))
    {
        return "GlobalICFGNode";
    }
    else if (SVFUtil::isa<IntraICFGNode>(node))
    {
        return "IntraICFGNode";
    }
    else if (SVFUtil::isa<InterICFGNode>(node))
    {
        return "InterICFGNode";
    }
    else if (SVFUtil::isa<FunEntryICFGNode>(node))
    {
        return "FunEntryICFGNode";
    }
    else if (SVFUtil::isa<FunExitICFGNode>(node))
    {
        return "FunExitICFGNode";
    }
    else if (SVFUtil::isa<CallICFGNode>(node))
    {
        return "CallICFGNode";
    }
    else if (SVFUtil::isa<RetICFGNode>(node))
    {
        return "RetICFGNode";
    }
    else 
    {
        assert("unknown icfg node type?");
        return "";
    }
}

std::string GraphDBClient::getIntraCFGEdgeStmt(const IntraCFGEdge* edge) {
    std::string srcKind = getICFGNodeKindString(edge->getSrcNode());
    std::string dstKind = getICFGNodeKindString(edge->getDstNode());
    std::string condition = "";
    if (edge->getCondition() != nullptr)
    {
        condition = ", condition_var_id:"+ std::to_string(edge->getCondition()->getId()) +
                    ", branch_cond_val:" + std::to_string(edge->getSuccessorCondValue());
    }
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"), (m:"+dstKind+") WHERE n.id = " +
        std::to_string(edge->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(edge->getDstNode()->getId()) +
        " CREATE (n)-[r:IntraCFGEdge{kind:" + std::to_string(edge->getEdgeKind()) +
        condition +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::getCallCFGEdgeStmt(const CallCFGEdge* edge) {
    std::string srcKind = getICFGNodeKindString(edge->getSrcNode());
    std::string dstKind = getICFGNodeKindString(edge->getDstNode());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"), (m:"+dstKind+") WHERE n.id = " +
        std::to_string(edge->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(edge->getDstNode()->getId()) +
        " CREATE (n)-[r:CallCFGEdge{kind:" + std::to_string(edge->getEdgeKind()) +
        ", call_pe_ids:'"+ extractEdgesIds(edge->getCallPEs()) +
        "'}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::getRetCFGEdgeStmt(const RetCFGEdge* edge) {
    std::string srcKind = getICFGNodeKindString(edge->getSrcNode());
    std::string dstKind = getICFGNodeKindString(edge->getDstNode());
    std::string ret_pe_id ="";
    if (edge->getRetPE() != nullptr)
    {
        ret_pe_id = ", ret_pe_id:"+ std::to_string(edge->getRetPE()->getEdgeID());
    }
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"), (m:"+dstKind+") WHERE n.id = " +
        std::to_string(edge->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(edge->getDstNode()->getId()) +
        " CREATE (n)-[r:RetCFGEdge{kind:" + std::to_string(edge->getEdgeKind()) +
        ret_pe_id+
        "}]->(m)";
    return queryStatement;
}

void GraphDBClient::insertICFG2db(const ICFG* icfg)
{
    // add all ICFG Node & Edge to DB
    if (nullptr != connection)
    {
        // create a new graph name ICFG in db
        createSubGraph(connection, "ICFG");
        // load schema for CallGraph
        /// TODO: schema path
        std::string ICFGNodePath =
            SVF_ROOT "/svf/include/Graphs/DBSchema/ICFGNodeSchema.json";
        std::string ICFGEdgePath =
            SVF_ROOT "/svf/include/Graphs/DBSchema/ICFGEdgeSchema.json";
        loadSchema(connection, ICFGNodePath.c_str(), "ICFG");
        loadSchema(connection, ICFGEdgePath.c_str(), "ICFG");
        for (auto it = icfg->begin(); it != icfg->end(); ++it)
        {
            ICFGNode* node = it->second;
            addICFGNode2db(connection, node, "ICFG");
            for (auto edgeIter = node->OutEdgeBegin();
                 edgeIter != node->OutEdgeEnd(); ++edgeIter)
            {
                ICFGEdge* edge = *edgeIter;
                addICFGEdge2db(connection, edge, "ICFG");
            }
        }
    }
}

void GraphDBClient::insertCallGraph2db(const CallGraph* callGraph)
{

    std::string callGraphNodePath =
        SVF_ROOT "/svf/include/Graphs/DBSchema/CallGraphNodeSchema.json";
    std::string callGraphEdgePath =
        SVF_ROOT "/svf/include/Graphs/DBSchema/CallGraphEdgeSchema.json";
    // add all CallGraph Node & Edge to DB
    if (nullptr != connection)
    {
        // create a new graph name CallGraph in db
        createSubGraph(connection, "CallGraph");
        // load schema for CallGraph
        /// TODO: schema path
        SVF::GraphDBClient::getInstance().loadSchema(
            connection,
            callGraphEdgePath,
            "CallGraph");
        SVF::GraphDBClient::getInstance().loadSchema(
            connection,
            callGraphNodePath,
            "CallGraph");
        for (const auto& item : *callGraph)
        {
            const CallGraphNode* node = item.second;
            SVF::GraphDBClient::getInstance().addCallGraphNode2db(
                connection, node, "CallGraph");
            for (CallGraphEdge::CallGraphEdgeSet::iterator iter =
                     node->OutEdgeBegin();
                 iter != node->OutEdgeEnd(); ++iter)
            {
                const CallGraphEdge* edge = *iter;
                SVF::GraphDBClient::getInstance().addCallGraphEdge2db(
                    connection, edge, "CallGraph");
            }
        }
        } else {
        SVFUtil::outs() << "No DB connection, skip inserting CallGraph to DB\n";
        }
}