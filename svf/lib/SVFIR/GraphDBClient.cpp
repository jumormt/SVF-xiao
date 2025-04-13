#include "SVFIR/GraphDBClient.h"
#include "SVFIR/SVFVariables.h"

using namespace SVF;

Map<int,FunObjVar*> id2funObjVarsMap;
Set<SVFBasicBlock*> basicBlocks;
Map<int, RetICFGNode*> id2RetICFGNodeMap;

bool GraphDBClient::loadSchema(lgraph::RpcClient* connection,
                               const std::string& filepath,
                               const std::string& dbname)
{
    if (nullptr != connection)
    {
        SVFUtil::outs() << "load schema from file:" << filepath << "\n";
        std::string result;
        bool ret = connection->ImportSchemaFromFile(result, filepath, dbname);
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
        connection->CallCypher(result, "CALL dbms.graph.deleteGraph('"+graphname+"')");
        bool ret = connection->CallCypherToLeader(result, "CALL dbms.graph.createGraph('"+graphname+"')");
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
        // SVFUtil::outs() << "ICFGEdge Query Statement:" << queryStatement << "\n";
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

        // SVFUtil::outs()<<"ICFGNode Insert Query:"<<queryStatement<<"\n";
        std::string result;
        if (queryStatement.empty())
        {
            return false;
        }
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
        // SVFUtil::outs()<<"CallGraph Node Insert Query:"<<queryStatement<<"\n";
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
            "MATCH (n:CallGraphNode{id:"+std::to_string(edge->getSrcNode()->getId())+"}), (m:CallGraphNode{id:"+std::to_string(edge->getDstNode()->getId()) + "}) WHERE n.id = " +
            std::to_string(edge->getSrcNode()->getId()) +
            " AND m.id = " + std::to_string(edge->getDstNode()->getId()) +
            " CREATE (n)-[r:CallGraphEdge{csid:" + std::to_string(edge->getCallSiteID()) +
            ", kind:" + std::to_string(edge->getEdgeKind()) +
            ", direct_call_set:'" + directCallIds + "', indirect_call_set:'" +
            indirectCallIds + "'}]->(m)";
        // SVFUtil::outs() << "Call Graph Edge Insert Query:" << queryStatement << "\n";
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
    ", fun_obj_var_id:" + std::to_string(node->getFun()->getId()) +
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
    ", bb_id:" + std::to_string(node->getBB()->getId()) +
    ", fp_nodes:'" + extractNodesIds(node->getFormalParms()) +"'})";
    return queryStatement;
}

std::string GraphDBClient::getFunExitICFGNodeInsertStmt(const FunExitICFGNode* node) {
    std::string formalRetId = "";
    if (nullptr == node->getFormalRet())
    {
        formalRetId = ",formal_ret_node_id:-1";
    } else {
        formalRetId = ",formal_ret_node_id:" + std::to_string(node->getFormalRet()->getId());
    }
    const std::string queryStatement ="CREATE (n:FunExitICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    ", fun_obj_var_id:" + std::to_string(node->getFun()->getId()) + 
    ", bb_id:" + std::to_string(node->getBB()->getId()) +
    formalRetId + "})";
    return queryStatement;
}

std::string GraphDBClient::getCallICFGNodeInsertStmt(const CallICFGNode* node) {
    std::string fun_name_of_v_call = "";
    std::string vtab_ptr_node_id = "";
    std::string virtual_fun_idx = "";
    std::string is_vir_call_inst = node->isVirtualCall() ? "true" : "false";
    std::string virtualFunAppendix = "";
    if (node->isVirtualCall())
    {
        fun_name_of_v_call = ", fun_name_of_v_call: '"+node->getFunNameOfVirtualCall()+"'";
        vtab_ptr_node_id = ", vtab_ptr_node_id:" + std::to_string(node->getVtablePtr()->getId());
        virtual_fun_idx = ", virtual_fun_idx:" + std::to_string(node->getFunIdxInVtable());
        virtualFunAppendix = vtab_ptr_node_id+virtual_fun_idx+fun_name_of_v_call;
    }
    else 
    {
        vtab_ptr_node_id = ", vtab_ptr_node_id:-1";
        virtual_fun_idx = ", virtual_fun_idx:-1";
        virtualFunAppendix = vtab_ptr_node_id+virtual_fun_idx;
    }
    std::string called_fun_obj_var_id = "";
    if (node->getCalledFunction() != nullptr)
    {
        called_fun_obj_var_id = ", called_fun_obj_var_id:" + std::to_string(node->getCalledFunction()->getId());
    }
    else 
    {
        called_fun_obj_var_id = ", called_fun_obj_var_id: -1";
    }
    std::string ret_icfg_node_id = "";
    if (node->getRetICFGNode() != nullptr)
    {
        ret_icfg_node_id = ", ret_icfg_node_id: " + std::to_string(node->getRetICFGNode()->getId());
    }
    else 
    {
        ret_icfg_node_id = ", ret_icfg_node_id: -1";
    }
    const std::string queryStatement ="CREATE (n:CallICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    ret_icfg_node_id +
    ", bb_id: " + std::to_string(node->getBB()->getId()) +
    ", fun_obj_var_id: " + std::to_string(node->getFun()->getId()) +
    ", svf_type:'" + node->getType()->toString() + "'" +
    ", ap_nodes:'" + extractNodesIds(node->getActualParms()) +"'" +
    called_fun_obj_var_id +
    ", is_vararg: " + (node->isVarArg() ? "true" : "false") +
    ", is_vir_call_inst: " + (node->isVirtualCall() ? "true" : "false") +
    virtualFunAppendix+"})";
    return queryStatement;
}

std::string GraphDBClient::getRetICFGNodeInsertStmt(const RetICFGNode* node) {
    std::string actual_ret_node_id="";
    if (node->getActualRet() != nullptr)
    {
        actual_ret_node_id = ", actual_ret_node_id: " + std::to_string(node->getActualRet()->getId()) ;
    } else {
        actual_ret_node_id = ", actual_ret_node_id: -1";
    }
    const std::string queryStatement ="CREATE (n:RetICFGNode {id: " + std::to_string(node->getId()) +
    ", kind: " + std::to_string(node->getNodeKind()) +
    actual_ret_node_id+
    ", call_block_node_id: " + std::to_string(node->getCallICFGNode()->getId()) +
    ", bb_id: " + std::to_string(node->getBB()->getId()) +
    ", fun_obj_var_id: " + std::to_string(node->getFun()->getId()) +
    ", svf_type:'" + node->getType()->toString() + "'"+"})";
    return queryStatement;
}

std::string GraphDBClient::getICFGNodeKindString(const ICFGNode* node)
{
    if(SVFUtil::isa<GlobalICFGNode>(node))
    {
        return "GlobalICFGNode";
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
    else if (SVFUtil::isa<InterICFGNode>(node))
    {
        return "InterICFGNode";
    }
    else if (SVFUtil::isa<IntraICFGNode>(node))
    {
        return "IntraICFGNode";
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
    } else {
        condition = ", condition_var_id:-1, branch_cond_val:-1";
    }
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getDstNode()->getId())+"}) WHERE n.id = " +
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
    "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getDstNode()->getId())+"}) WHERE n.id = " +
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
    } else {
        ret_pe_id = ", ret_pe_id:-1";
    }
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getDstNode()->getId())+"}) WHERE n.id = " +
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
        std::string ICFGNodePath =
            SVF_ROOT "/svf/include/Graphs/DBSchema/ICFGNodeSchema.json";
        std::string ICFGEdgePath =
            SVF_ROOT "/svf/include/Graphs/DBSchema/ICFGEdgeSchema.json";
        loadSchema(connection, ICFGNodePath.c_str(), "ICFG");
        loadSchema(connection, ICFGEdgePath.c_str(), "ICFG");
        std::vector<const ICFGEdge*> edges;
        for (auto it = icfg->begin(); it != icfg->end(); ++it)
        {
            ICFGNode* node = it->second;
            addICFGNode2db(connection, node, "ICFG");
            for (auto edgeIter = node->OutEdgeBegin();
                 edgeIter != node->OutEdgeEnd(); ++edgeIter)
            {
                ICFGEdge* edge = *edgeIter;
                edges.push_back(edge);
            }
        }
        for (auto edge : edges)
        {
            addICFGEdge2db(connection, edge, "ICFG");
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
        SVF::GraphDBClient::getInstance().loadSchema(
            connection,
            callGraphEdgePath,
            "CallGraph");
        SVF::GraphDBClient::getInstance().loadSchema(
            connection,
            callGraphNodePath,
            "CallGraph");
        std::vector<const CallGraphEdge*> edges;
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
                edges.push_back(edge);
            }
        }
        for (const auto& edge : edges)
        {
            SVF::GraphDBClient::getInstance().addCallGraphEdge2db(connection, edge, "CallGraph");
        }

        } else {
        SVFUtil::outs() << "No DB connection, skip inserting CallGraph to DB\n";
        }
}

void GraphDBClient::insertSVFTypeNodeSet2db(const Set<const SVFType*>* types, const Set<const StInfo*>* stInfos, std::string& dbname)
{
    if (nullptr != connection)
    {
        // create a new graph name SVFType in db
        createSubGraph(connection, "SVFType");
        // load schema for SVFType
        loadSchema(connection, SVF_ROOT "/svf/include/Graphs/DBSchema/SVFTypeNodeSchema.json", "SVFType");
        
        // load & insert each svftype node to db
        for (const auto& ty : *types)
        {
            std::string queryStatement;
            if (SVFUtil::isa<SVFPointerType>(ty))
            {
                queryStatement = getSVFPointerTypeNodeInsertStmt(SVFUtil::cast<SVFPointerType>(ty));
            } 
            else if (SVFUtil::isa<SVFIntegerType>(ty))
            {
                queryStatement = getSVFIntegerTypeNodeInsertStmt(SVFUtil::cast<SVFIntegerType>(ty));
            }
            else if (SVFUtil::isa<SVFFunctionType>(ty))
            {
                queryStatement = getSVFFunctionTypeNodeInsertStmt(SVFUtil::cast<SVFFunctionType>(ty));
            }
            else if (SVFUtil::isa<SVFStructType>(ty))
            {
                queryStatement = getSVFSturctTypeNodeInsertStmt(SVFUtil::cast<SVFStructType>(ty));  
            }
            else if (SVFUtil::isa<SVFArrayType>(ty))
            {
                queryStatement = getSVFArrayTypeNodeInsertStmt(SVFUtil::cast<SVFArrayType>(ty));
            }
            else if (SVFUtil::isa<SVFOtherType>(ty))
            {
                queryStatement = getSVFOtherTypeNodeInsertStmt(SVFUtil::cast<SVFOtherType>(ty));   
            }
            else 
            {
                assert("unknown SVF type?");
                return ;
            }
    
            // SVFUtil::outs()<<"SVFType Insert Query:"<<queryStatement<<"\n";
            std::string result;
            bool ret = connection->CallCypher(result, queryStatement, dbname);
            if (ret)
            {
                SVFUtil::outs()<< "SVFType node added: " << result << "\n";
            }
            else
            {
                SVFUtil::outs() << "Failed to add SVFType node to db " << dbname << " "
                                << result << "\n";
            }
        
        }

        // load & insert each stinfo node to db
        for(const auto& stInfo : *stInfos)
        {
            // insert stinfo node to db
            std::string queryStatement = getStInfoNodeInsertStmt(stInfo);
            // SVFUtil::outs()<<"StInfo Insert Query:"<<queryStatement<<"\n";
            std::string result;
            bool ret = connection->CallCypher(result, queryStatement, dbname);
            if (ret)
            {
                SVFUtil::outs()<< "StInfo node added: " << result << "\n";
            }
            else
            {
                SVFUtil::outs() << "Failed to add StInfo node to db " << dbname << " "
                                << result << "\n";
            }
        }
    }

}

std::string GraphDBClient::getSVFPointerTypeNodeInsertStmt(const SVFPointerType* node)
{
    std::string is_single_val_ty = node->isSingleValueType() ? "true" : "false";
    const std::string queryStatement ="CREATE (n:SVFPointerType {type_name:'" + node->toString() +
    "', svf_i8_type_name:'" + node->getSVFInt8Type()->toString() +
    "', svf_ptr_type_name:'" + node->getSVFPtrType()->toString() + 
    "', kind:" + std::to_string(node->getKind()) + 
    ", is_single_val_ty:" + is_single_val_ty + 
    ", byte_size:" + std::to_string(node->getByteSize()) + "})";
    return queryStatement;
}

std::string GraphDBClient::getSVFIntegerTypeNodeInsertStmt(const SVFIntegerType* node)
{
    std::string is_single_val_ty = node->isSingleValueType() ? "true" : "false";
    const std::string queryStatement ="CREATE (n:SVFIntegerType {type_name:'" + node->toString() +
    "', svf_i8_type_name:'" + node->getSVFInt8Type()->toString() +
    "', svf_ptr_type_name:'" + node->getSVFPtrType()->toString() + 
    "', kind:" + std::to_string(node->getKind()) + 
    ", is_single_val_ty:" + is_single_val_ty + 
    ", byte_size:" + std::to_string(node->getByteSize()) +
    ", single_and_width:" + std::to_string(node->getSignAndWidth()) + "})";
    return queryStatement;
}

std::string GraphDBClient::getSVFFunctionTypeNodeInsertStmt(const SVFFunctionType* node)
{
    std::string is_single_val_ty = node->isSingleValueType() ? "true" : "false";
    const std::string queryStatement ="CREATE (n:SVFFunctionType {type_name:'" + node->toString() +
    "', svf_i8_type_name:'" + node->getSVFInt8Type()->toString() +
    "', svf_ptr_type_name:'" + node->getSVFPtrType()->toString() + 
    "', kind:" + std::to_string(node->getKind()) + 
    ", is_single_val_ty:" + is_single_val_ty + 
    ", byte_size:" + std::to_string(node->getByteSize()) +
    ", params_types_vec:'" + extractSVFTypes(node->getParamTypes()) +
    "', ret_ty_node_name:'" + node->getReturnType()->toString() + "'})";
    return queryStatement;
}

std::string GraphDBClient::getSVFSturctTypeNodeInsertStmt(const SVFStructType* node)
{
    std::string is_single_val_ty = node->isSingleValueType() ? "true" : "false";
    const std::string queryStatement ="CREATE (n:SVFStructType {type_name:'" + node->toString() +
    "', svf_i8_type_name:'" + node->getSVFInt8Type()->toString() +
    "', svf_ptr_type_name:'" + node->getSVFPtrType()->toString() + 
    "', kind:" + std::to_string(node->getKind()) + 
    ", stinfo_node_id:" + std::to_string(node->getTypeInfo()->getStinfoId()) +
    ", is_single_val_ty:" + is_single_val_ty + 
    ", byte_size:" + std::to_string(node->getByteSize()) +
    ", struct_name:'" + node->getName() + "'})";
    return queryStatement;
}

std::string GraphDBClient::getSVFArrayTypeNodeInsertStmt(const SVFArrayType* node)
{
    std::string is_single_val_ty = node->isSingleValueType() ? "true" : "false";
    const std::string queryStatement ="CREATE (n:SVFArrayType {type_name:'" + node->toString() +
    "', svf_i8_type_name:'" + node->getSVFInt8Type()->toString() +
    "', svf_ptr_type_name:'" + node->getSVFPtrType()->toString() + 
    "', kind:" + std::to_string(node->getKind()) + 
    ", stinfo_node_id:" + std::to_string(node->getTypeInfo()->getStinfoId()) +
    ", is_single_val_ty:" + is_single_val_ty + 
    ", byte_size:" + std::to_string(node->getByteSize()) +
    ", num_of_element:" + std::to_string(node->getNumOfElement()) + 
    ", type_of_element_node_type_name:'" + node->getTypeOfElement()->toString() + "'})";
    return queryStatement;
}

std::string GraphDBClient::getSVFOtherTypeNodeInsertStmt(const SVFOtherType* node)
{
    std::string is_single_val_ty = node->isSingleValueType() ? "true" : "false";
    const std::string queryStatement ="CREATE (n:SVFOtherType {type_name:'" + node->toString() +
    "', svf_i8_type_name:'" + node->getSVFInt8Type()->toString() +
    "', svf_ptr_type_name:'" + node->getSVFPtrType()->toString() + 
    "', kind:" + std::to_string(node->getKind()) + 
    ", is_single_val_ty:" + is_single_val_ty + 
    ", byte_size:" + std::to_string(node->getByteSize()) +
    ", repr:'" + node->getRepr() + "'})";
    return queryStatement;
}

std::string GraphDBClient::getStInfoNodeInsertStmt(const StInfo* node)
{
    const std::string queryStatement ="CREATE (n:StInfo {id:" + std::to_string(node->getStinfoId()) +
    ", fld_idx_vec:'" + extractIdxs(node->getFlattenedFieldIdxVec()) +
    "', elem_idx_vec:'" + extractIdxs(node->getFlattenedElemIdxVec()) + 
    "', finfo_types:'" + extractSVFTypes(node->getFlattenFieldTypes()) + 
    "', flatten_element_types:'" + extractSVFTypes(node->getFlattenElementTypes()) + 
    "', fld_idx_2_type_map:'" + extractFldIdx2TypeMap(node->getFldIdx2TypeMap()) +
    "', stride:" + std::to_string(node->getStride()) +
    ", num_of_flatten_elements:" + std::to_string(node->getNumOfFlattenElements()) +
    ", num_of_flatten_fields:" + std::to_string(node->getNumOfFlattenFields()) + "})";
    return queryStatement;
}

void GraphDBClient::insertBasicBlockGraph2db(const BasicBlockGraph* bbGraph)
{
    if (nullptr != connection)
    {
        std::vector<const BasicBlockEdge*> edges;
        for (auto& bb: *bbGraph)
        {
            SVFBasicBlock* node = bb.second;
            insertBBNode2db(connection, node, "BasicBlockGraph");
            for (auto iter = node->OutEdgeBegin(); iter != node->OutEdgeEnd(); ++iter)
            {
                edges.push_back(*iter);
            }
        }
        for (const BasicBlockEdge* edge : edges)
        {
            insertBBEdge2db(connection, edge, "BasicBlockGraph");
        }
    }
}

void GraphDBClient::insertBBEdge2db(lgraph::RpcClient* connection, const BasicBlockEdge* edge, const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string queryStatement = getBBEdgeInsertStmt(edge);
        // SVFUtil::outs()<<"BBEdge Insert Query:"<<queryStatement<<"\n";
        std::string result;
        if (!queryStatement.empty())
        {
            bool ret = connection->CallCypher(result, queryStatement, dbname);
            if (ret)
            {
                SVFUtil::outs() << "BB edge added: " << result << "\n";
            }
            else
            {
                SVFUtil::outs() << "Failed to add BB edge to db " << dbname
                                << " " << result << "\n";
            }
        }
    }
}

void GraphDBClient::insertBBNode2db(lgraph::RpcClient* connection, const SVFBasicBlock* node, const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string queryStatement = getBBNodeInsertStmt(node);
        // SVFUtil::outs()<<"BBNode Insert Query:"<<queryStatement<<"\n";
        std::string result;
        if (!queryStatement.empty())
        {
            bool ret = connection->CallCypher(result, queryStatement, dbname);
            if (ret)
            {
                SVFUtil::outs() << "BB node added: " << result << "\n";
            }
            else
            {
                SVFUtil::outs() << "Failed to add BB node to db " << dbname
                                << " " << result << "\n";
            }
        }
    }
}

std::string GraphDBClient::getBBNodeInsertStmt(const SVFBasicBlock* node)
{
    const std::string queryStatement ="CREATE (n:SVFBasicBlock {id:'" + std::to_string(node->getId())+":" + std::to_string(node->getFunction()->getId()) + "'" +
    ", fun_obj_var_id: " + std::to_string(node->getFunction()->getId()) +
    ", bb_name:'" + node->getName() +"'" +
    ", sscc_bb_ids:'" + extractNodesIds(node->getSuccBBs()) + "'" +
    ", pred_bb_ids:'" + extractNodesIds(node->getPredBBs()) + "'" +
    ", all_icfg_nodes_ids:'" + extractNodesIds(node->getICFGNodeList()) + "'" +
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getBBEdgeInsertStmt(const BasicBlockEdge* edge)
{
    const std::string queryStatement =
    "MATCH (n:SVFBasicBlock {id:'"+std::to_string(edge->getSrcID())+":"+std::to_string(edge->getSrcNode()->getFunction()->getId())+"'}), (m:SVFBasicBlock{id:'"+std::to_string(edge->getDstID())+":"+std::to_string(edge->getDstNode()->getFunction()->getId())+
    "'}) WHERE n.id = '" +std::to_string(edge->getSrcID())+":" + std::to_string(edge->getSrcNode()->getFunction()->getId())+ "'"+
    " AND m.id = '" +std::to_string(edge->getDstID())+":" + std::to_string(edge->getDstNode()->getFunction()->getId())+ "'"+
    " CREATE (n)-[r:BasicBlockEdge{}]->(m)";
    return queryStatement;
}

void GraphDBClient::insertPAG2db(const PAG* pag)
{
    std::string pagNodePath =
        SVF_ROOT "/svf/include/Graphs/DBSchema/PAGNodeSchema.json";
    std::string pagEdgePath =
        SVF_ROOT "/svf/include/Graphs/DBSchema/PAGEdgeSchema.json";
    std::string bbNodePath =
        SVF_ROOT "/svf/include/Graphs/DBSchema/BasicBlockNodeSchema.json";
    std::string bbEdgePath =
        SVF_ROOT "/svf/include/Graphs/DBSchema/BasicBlockEdgeSchema.json";

    // add all PAG Node & Edge to DB
    if (nullptr != connection)
    {
        // create a new graph name PAG in db
        createSubGraph(connection, "PAG");
        // create a new graph name BasicBlockGraph in db
        createSubGraph(connection, "BasicBlockGraph");
        // load schema for PAG
        SVF::GraphDBClient::getInstance().loadSchema(connection, pagEdgePath,
                                                     "PAG");
        SVF::GraphDBClient::getInstance().loadSchema(connection, pagNodePath,
                                                     "PAG");
        // load schema for PAG
        SVF::GraphDBClient::getInstance().loadSchema(connection, bbEdgePath,
                                                     "BasicBlockGraph");
        SVF::GraphDBClient::getInstance().loadSchema(connection, bbNodePath,
                                                     "BasicBlockGraph");

        std::vector<const SVFStmt*> edges;
        for (auto it = pag->begin(); it != pag->end(); ++it)
        {
            SVFVar* node = it->second;
            insertPAGNode2db(connection, node, "PAG");
            for (auto edgeIter = node->OutEdgeBegin();
                 edgeIter != node->OutEdgeEnd(); ++edgeIter)
            {
                SVFStmt* edge = *edgeIter;
                edges.push_back(edge);
            }
        }
        for (auto edge : edges)
        {
            insertPAGEdge2db(connection, edge, "PAG");
        }
    }
    else
    {
        SVFUtil::outs() << "No DB connection, skip inserting CallGraph to DB\n";
    }
}

void GraphDBClient::insertPAGEdge2db(lgraph::RpcClient* connection, const SVFStmt* edge, const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string queryStatement = getPAGEdgeInsertStmt(edge);
        // SVFUtil::outs()<<"PAGEdge Insert Query:"<<queryStatement<<"\n";
        std::string result;
        if (!queryStatement.empty())
        {
            bool ret = connection->CallCypher(result, queryStatement, dbname);
            if (ret)
            {
                SVFUtil::outs() << "PAG edge added: " << result << "\n";
            }
            else
            {
                SVFUtil::outs() << "Failed to add PAG edge to db " << dbname
                                << " " << result << "\n";
            }
        }
    }
}

std::string GraphDBClient::getPAGEdgeInsertStmt(const SVFStmt* edge)
{
    std::string queryStatement = "";
    if(SVFUtil::isa<TDForkPE>(edge))
    {
        queryStatement = generateTDForkPEEdgeInsertStmt(SVFUtil::cast<TDForkPE>(edge));
    } 
    else if(SVFUtil::isa<TDJoinPE>(edge))
    {
        queryStatement = generateTDJoinPEEdgeInsertStmt(SVFUtil::cast<TDJoinPE>(edge));
    }
    else if(SVFUtil::isa<CallPE>(edge))
    {
        queryStatement = generateCallPEEdgeInsertStmt(SVFUtil::cast<CallPE>(edge));
    }
    else if(SVFUtil::isa<RetPE>(edge))
    {
        queryStatement = generateRetPEEdgeInsertStmt(SVFUtil::cast<RetPE>(edge));
    }
    else if(SVFUtil::isa<GepStmt>(edge))
    {
        queryStatement = generateGepStmtEdgeInsertStmt(SVFUtil::cast<GepStmt>(edge));
    }
    else if(SVFUtil::isa<LoadStmt>(edge))
    {
        queryStatement = generateLoadStmtEdgeInsertStmt(SVFUtil::cast<LoadStmt>(edge));
    }
    else if(SVFUtil::isa<StoreStmt>(edge))
    {
        queryStatement = generateStoreStmtEdgeInsertStmt(SVFUtil::cast<StoreStmt>(edge));
    }
    else if(SVFUtil::isa<CopyStmt>(edge))
    {
        queryStatement = generateCopyStmtEdgeInsertStmt(SVFUtil::cast<CopyStmt>(edge));
    }
    else if(SVFUtil::isa<AddrStmt>(edge))
    {
        queryStatement = generateAddrStmtEdgeInsertStmt(SVFUtil::cast<AddrStmt>(edge));
    }
    else if(SVFUtil::isa<AssignStmt>(edge))
    {
        queryStatement = generateAssignStmtEdgeInsertStmt(SVFUtil::cast<AssignStmt>(edge));
    }
    else if(SVFUtil::isa<PhiStmt>(edge))
    {
        queryStatement = generatePhiStmtEdgeInsertStmt(SVFUtil::cast<PhiStmt>(edge));
    }
    else if(SVFUtil::isa<SelectStmt>(edge))
    {
        queryStatement = generateSelectStmtEndgeInsertStmt(SVFUtil::cast<SelectStmt>(edge));
    }
    else if(SVFUtil::isa<CmpStmt>(edge))
    {
        queryStatement = generateCmpStmtEdgeInsertStmt(SVFUtil::cast<CmpStmt>(edge));
    }
    else if(SVFUtil::isa<BinaryOPStmt>(edge))
    {
        queryStatement = generateBinaryOPStmtEdgeInsertStmt(SVFUtil::cast<BinaryOPStmt>(edge));
    }
    else if(SVFUtil::isa<MultiOpndStmt>(edge))
    {
        queryStatement = generateMultiOpndStmtEdgeInsertStmt(SVFUtil::cast<MultiOpndStmt>(edge));
    }
    else if(SVFUtil::isa<UnaryOPStmt>(edge))
    {
        queryStatement = genereateUnaryOPStmtEdgeInsertStmt(SVFUtil::cast<UnaryOPStmt>(edge));
    }
    else if(SVFUtil::isa<BranchStmt>(edge))
    {
        queryStatement = generateBranchStmtEdgeInsertStmt(SVFUtil::cast<BranchStmt>(edge));
    }
    else if(SVFUtil::isa<SVFStmt>(edge))
    {
        queryStatement = generateSVFStmtEdgeInsertStmt(SVFUtil::cast<SVFStmt>(edge));
    }
    else
    {
        assert("unknown SVFStmt type?");
    }
    return queryStatement;
}

void GraphDBClient::insertPAGNode2db(lgraph::RpcClient* connection, const SVFVar* node, const std::string& dbname)
{
    if (nullptr != connection)
    {
        std::string queryStatement = getPAGNodeInsertStmt(node);
        // SVFUtil::outs()<<"PAGNode Insert Query:"<<queryStatement<<"\n";
        std::string result;
        if (!queryStatement.empty())
        {
            bool ret = connection->CallCypher(result, queryStatement, dbname);
            if (ret)
            {
                SVFUtil::outs() << "PAG node added: " << result << "\n";
            }
            else
            {
                SVFUtil::outs() << "Failed to add PAG node to db " << dbname
                                << " " << result << "\n";
            }
        }
    }
}

std::string GraphDBClient::getPAGNodeInsertStmt(const SVFVar* node)
{
    std::string queryStatement = "";
    if(SVFUtil::isa<ConstNullPtrValVar>(node))
    {
        queryStatement = getConstNullPtrValVarNodeInsertStmt(SVFUtil::cast<ConstNullPtrValVar>(node));
    }
    else if(SVFUtil::isa<ConstIntValVar>(node))
    {
        queryStatement = getConstIntValVarNodeInsertStmt(SVFUtil::cast<ConstIntValVar>(node));
    }
    else if(SVFUtil::isa<ConstFPValVar>(node))
    {
        queryStatement = getConstFPValVarNodeInsertStmt(SVFUtil::cast<ConstFPValVar>(node));
    }
    else if(SVFUtil::isa<BlackHoleValVar>(node))
    {
        queryStatement = getBlackHoleValvarNodeInsertStmt(SVFUtil::cast<BlackHoleValVar>(node));
    }
    else if(SVFUtil::isa<ConstDataValVar>(node))
    {
        queryStatement = getConstDataValVarNodeInsertStmt(SVFUtil::cast<ConstDataValVar>(node));
    }
    else if(SVFUtil::isa<RetValPN>(node))
    {
        queryStatement = getRetValPNNodeInsertStmt(SVFUtil::cast<RetValPN>(node));
    }
    else if(SVFUtil::isa<VarArgValPN>(node))
    {
        queryStatement = getVarArgValPNNodeInsertStmt(SVFUtil::cast<VarArgValPN>(node));
    }
    else if(SVFUtil::isa<DummyValVar>(node))
    {
        queryStatement = getDummyValVarNodeInsertStmt(SVFUtil::cast<DummyValVar>(node));
    }
    else if(SVFUtil::isa<ConstAggValVar>(node))
    {
        queryStatement = getConstAggValVarNodeInsertStmt(SVFUtil::cast<ConstAggValVar>(node));
    }
    else if(SVFUtil::isa<GlobalValVar>(node))
    {
        queryStatement = getGlobalValVarNodeInsertStmt(SVFUtil::cast<GlobalValVar>(node));
    }
    else if(SVFUtil::isa<FunValVar>(node))
    {
        queryStatement = getFunValVarNodeInsertStmt(SVFUtil::cast<FunValVar>(node));
    }
    else if(SVFUtil::isa<GepValVar>(node))
    {
        queryStatement = getGepValVarNodeInsertStmt(SVFUtil::cast<GepValVar>(node));
    }
    else if(SVFUtil::isa<ArgValVar>(node))
    {
        queryStatement = getArgValVarNodeInsertStmt(SVFUtil::cast<ArgValVar>(node));
    }
    else if(SVFUtil::isa<ValVar>(node))
    {
        queryStatement = getValVarNodeInsertStmt(SVFUtil::cast<ValVar>(node));
    }
    else if(SVFUtil::isa<ConstNullPtrObjVar>(node))
    {
        queryStatement = getConstNullPtrObjVarNodeInsertStmt(SVFUtil::cast<ConstNullPtrObjVar>(node));
    }
    else if(SVFUtil::isa<ConstIntObjVar>(node))
    {
        queryStatement = getConstIntObjVarNodeInsertStmt(SVFUtil::cast<ConstIntObjVar>(node));
    }
    else if(SVFUtil::isa<ConstFPObjVar>(node))
    {
        queryStatement = getConstFPObjVarNodeInsertStmt(SVFUtil::cast<ConstFPObjVar>(node));
    }
    else if(SVFUtil::isa<ConstDataObjVar>(node))
    {
        queryStatement = getConstDataObjVarNodeInsertStmt(SVFUtil::cast<ConstDataObjVar>(node));
    }
    else if(SVFUtil::isa<DummyObjVar>(node))
    {
        queryStatement = getDummyObjVarNodeInsertStmt(SVFUtil::cast<DummyObjVar>(node));
    }
    else if(SVFUtil::isa<ConstAggObjVar>(node))
    {
        queryStatement = getConstAggObjVarNodeInsertStmt(SVFUtil::cast<ConstAggObjVar>(node));
    }
    else if(SVFUtil::isa<GlobalObjVar>(node))
    {
        queryStatement = getGlobalObjVarNodeInsertStmt(SVFUtil::cast<GlobalObjVar>(node));
    }
    else if(SVFUtil::isa<FunObjVar>(node))
    {
        const FunObjVar* funObjVar = SVFUtil::cast<FunObjVar>(node);
        queryStatement = getFunObjVarNodeInsertStmt(funObjVar);
        if ( nullptr != funObjVar->getBasicBlockGraph())
        {
            insertBasicBlockGraph2db(funObjVar->getBasicBlockGraph());
        }
    }
    else if(SVFUtil::isa<StackObjVar>(node))
    {
        queryStatement = getStackObjVarNodeInsertStmt(SVFUtil::cast<StackObjVar>(node));
    }
    else if(SVFUtil::isa<HeapObjVar>(node))
    {
        queryStatement = getHeapObjVarNodeInsertStmt(SVFUtil::cast<HeapObjVar>(node));
    } 
    else if(SVFUtil::isa<BaseObjVar>(node))
    {
        queryStatement = getBaseObjNodeInsertStmt(SVFUtil::cast<BaseObjVar>(node));
    }
    else if(SVFUtil::isa<GepObjVar>(node))
    {
        queryStatement = getGepObjVarNodeInsertStmt(SVFUtil::cast<GepObjVar>(node));
    }
    else if(SVFUtil::isa<ObjVar>(node))
    {
        queryStatement = getObjVarNodeInsertStmt(SVFUtil::cast<ObjVar>(node));
    }
    else
    {
        assert("unknown SVFVar type?");
    }
    return queryStatement;
}

std::string GraphDBClient::getSVFVarNodeFieldsStmt(const SVFVar* node)
{
    std::string fieldsStr = "";
    fieldsStr += "id: " + std::to_string(node->getId()) + 
    ", svf_type_name:'"+node->getType()->toString() +
    "', in_edge_kind_to_set_map:'" + pagEdgeToSetMapTyToString(node->getInEdgeKindToSetMap()) +
    "', out_edge_kind_to_set_map:'" + pagEdgeToSetMapTyToString(node->getOutEdgeKindToSetMap()) +
    "'";
    return fieldsStr;
}

std::string GraphDBClient::getValVarNodeFieldsStmt(const ValVar* node)
{
    std::string fieldsStr = getSVFVarNodeFieldsStmt(node);
    if ( nullptr != node->getICFGNode())
    {
        fieldsStr += ", icfg_node_id:" + std::to_string(node->getICFGNode()->getId());
    }
    else
    {
        fieldsStr += ", icfg_node_id:-1";
    }
    return fieldsStr;
}

std::string GraphDBClient::getValVarNodeInsertStmt(const ValVar* node)
{
    const std::string queryStatement ="CREATE (n:ValVar {"+
    getValVarNodeFieldsStmt(node)+
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstDataValVarNodeFieldsStmt(const ConstDataValVar* node)
{
    return getValVarNodeFieldsStmt(node);
}


std::string GraphDBClient::getConstDataValVarNodeInsertStmt(const ConstDataValVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstDataValVar {"+
    getConstDataValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getBlackHoleValvarNodeInsertStmt(const BlackHoleValVar* node)
{
    const std::string queryStatement ="CREATE (n:BlackHoleValVar {"+
    getConstDataValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstFPValVarNodeInsertStmt(const ConstFPValVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstFPValVar {"+
    getConstDataValVarNodeFieldsStmt(node) 
    +", kind:" + std::to_string(node->getNodeKind())
    +", dval:"+ std::to_string(node->getFPValue())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstIntValVarNodeInsertStmt(const ConstIntValVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstIntValVar {"+
    getConstDataValVarNodeFieldsStmt(node) 
    +", kind:" + std::to_string(node->getNodeKind())
    +", zval:'"+ std::to_string(node->getZExtValue()) + "'"
    +", sval:"+ std::to_string(node->getSExtValue())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstNullPtrValVarNodeInsertStmt(const ConstNullPtrValVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstNullPtrValVar {"+
    getConstDataValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getRetValPNNodeInsertStmt(const RetValPN* node)
{
    const std::string queryStatement ="CREATE (n:RetValPN {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    +", call_graph_node_id:"+std::to_string(node->getCallGraphNode()->getId())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getVarArgValPNNodeInsertStmt(const VarArgValPN* node)
{
    const std::string queryStatement ="CREATE (n:VarArgValPN {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    +", call_graph_node_id:"+std::to_string(node->getFunction()->getId())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getDummyValVarNodeInsertStmt(const DummyValVar* node)
{
    const std::string queryStatement ="CREATE (n:DummyValVar {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstAggValVarNodeInsertStmt(const ConstAggValVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstAggValVar {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getGlobalValVarNodeInsertStmt(const GlobalValVar* node)
{
    const std::string queryStatement ="CREATE (n:GlobalValVar {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getFunValVarNodeInsertStmt(const FunValVar* node)
{
    const std::string queryStatement ="CREATE (n:FunValVar {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + ", fun_obj_var_node_id:" + std::to_string(node->getFunction()->getId())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getGepValVarNodeInsertStmt(const GepValVar* node)
{std::ostringstream accessPathFieldsStr;
    accessPathFieldsStr << "";

    if (nullptr != node->getAccessPath().gepSrcPointeeType())
    {
        accessPathFieldsStr << ", ap_gep_pointee_type_name:'"<<node->getAccessPath().gepSrcPointeeType()->toString()<<"'";
    }
    if (!node->getAccessPath().getIdxOperandPairVec().empty())
    {
        accessPathFieldsStr <<", ap_idx_operand_pairs:'"<< IdxOperandPairsToString(&node->getAccessPath().getIdxOperandPairVec())<<"'";
    }
    const std::string queryStatement ="CREATE (n:GepValVar {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + ", base_val_id:" + std::to_string(node->getBaseNode()->getId())
    + ", gep_val_svf_type_name:'"+node->getType()->toString()+"'"
    + ", ap_fld_idx:"+std::to_string(node->getConstantFieldIdx())
    + accessPathFieldsStr.str()
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getArgValVarNodeInsertStmt(const ArgValVar* node)
{
    const std::string queryStatement ="CREATE (n:ArgValVar {"+
    getValVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + ", cg_node_id:" + std::to_string(node->getParent()->getId())
    + ", arg_no:" + std::to_string(node->getArgNo())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getObjVarNodeFieldsStmt(const ObjVar* node)
{
    return getSVFVarNodeFieldsStmt(node);
}

std::string GraphDBClient::getObjVarNodeInsertStmt(const ObjVar* node)
{
    const std::string queryStatement ="CREATE (n:ObjVar {"+
    getObjVarNodeFieldsStmt(node)
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getBaseObjVarNodeFieldsStmt(const BaseObjVar* node)
{
    std::string fieldsStr;
    std::string icfgIDstr = "";
    if ( nullptr != node->getICFGNode())
    {
        icfgIDstr = ", icfg_node_id:" + std::to_string(node->getICFGNode()->getId());
    }
    else 
    {
        icfgIDstr = ", icfg_node_id:-1";
    }
    std::string objTypeInfo_byteSize_str = "";
    if (node->isConstantByteSize())
    {
        objTypeInfo_byteSize_str += ", obj_type_info_byte_size:" + std::to_string(node->getByteSizeOfObj());
    }
    fieldsStr += getObjVarNodeFieldsStmt(node) +
    icfgIDstr + 
    ", obj_type_info_type_name:'" + node->getTypeInfo()->getType()->toString() + "'" + 
    ", obj_type_info_flags:" + std::to_string(node->getTypeInfo()->getFlag()) +
    ", obj_type_info_max_offset_limit:" + std::to_string(node->getMaxFieldOffsetLimit()) + 
    ", obj_type_info_elem_num:" + std::to_string(node->getNumOfElements()) +
    objTypeInfo_byteSize_str;
    return fieldsStr;
}

std::string GraphDBClient::getBaseObjNodeInsertStmt(const BaseObjVar* node)
{
    const std::string queryStatement ="CREATE (n:BaseObjVar {"+
    getBaseObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getGepObjVarNodeInsertStmt(const GepObjVar* node)
{
    const std::string queryStatement ="CREATE (n:BaseObjVar {"+
    getObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + ", base_obj_var_node_id:" + std::to_string(node->getBaseObj()->getId())
    + ", app_offset:" + std::to_string(node->getConstantFieldIdx())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getHeapObjVarNodeInsertStmt(const HeapObjVar* node)
{
    const std::string queryStatement ="CREATE (n:HeapObjVar {"+
    getBaseObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getStackObjVarNodeInsertStmt(const StackObjVar* node)
{
    const std::string queryStatement ="CREATE (n:StackObjVar {"+
    getBaseObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstDataObjVarNodeFieldsStmt(const ConstDataObjVar* node)
{
    return getBaseObjVarNodeFieldsStmt(node);
}

std::string GraphDBClient::getConstDataObjVarNodeInsertStmt(const ConstDataObjVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstDataObjVar {"+
    getConstDataObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstNullPtrObjVarNodeInsertStmt(const ConstNullPtrObjVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstNullPtrObjVar {"+
    getConstDataObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstIntObjVarNodeInsertStmt(const ConstIntObjVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstIntObjVar {"+
    getConstDataObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + ", zval:'" + std::to_string(node->getZExtValue()) + "'"
    + ", sval:" + std::to_string(node->getSExtValue())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstFPObjVarNodeInsertStmt(const ConstFPObjVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstFPObjVar {"+
    getConstDataObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + ", dval:" + std::to_string(node->getFPValue())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getDummyObjVarNodeInsertStmt(const DummyObjVar* node)
{
    const std::string queryStatement ="CREATE (n:DummyObjVar {"+
    getBaseObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getConstAggObjVarNodeInsertStmt(const ConstAggObjVar* node)
{
    const std::string queryStatement ="CREATE (n:ConstAggObjVar {"+
    getBaseObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getGlobalObjVarNodeInsertStmt(const GlobalObjVar* node)
{
    const std::string queryStatement ="CREATE (n:GlobalObjVar {"+
    getBaseObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + "})";
    return queryStatement;
}

std::string GraphDBClient::getFunObjVarNodeInsertStmt(const FunObjVar* node)
{
    /// TODO: add bbGraph to bbGraph_tuGraph DB
    std::ostringstream exitBBStr;
    if (node->hasBasicBlock() && nullptr != node->getExitBB())
    {
        exitBBStr << ", exit_bb_id:" << std::to_string(node->getExitBB()->getId());
    } 
    else 
    {
        exitBBStr << ", exit_bb_id:-1";
    }
    const std::string queryStatement ="CREATE (n:FunObjVar {"+
    getBaseObjVarNodeFieldsStmt(node) 
    + ", kind:" + std::to_string(node->getNodeKind())
    + ", is_decl:" + (node->isDeclaration()? "true" : "false")
    + ", intrinsic:" + (node->isIntrinsic()? "true" : "false")
    + ", is_addr_taken:" + (node->hasAddressTaken()? "true" : "false")
    + ", is_uncalled:" + (node->isUncalledFunction()? "true" : "false")
    + ", is_not_ret:" + (node->hasReturn()? "true" : "false")
    + ", sup_var_arg:" + (node->isVarArg()? "true" : "false")
    + ", fun_type_name:'" + node->getFunctionType()->toString() + "'"
    + ", real_def_fun_node_id:" + std::to_string(node->getDefFunForMultipleModule()->getId())
    // + ", bb_graph_id:" + std::to_string(node->getBasicBlockGraph()->getFunObjVarId())
    + exitBBStr.str()
    + ", all_args_node_ids:'" + extractNodesIds(node->getArgs()) + "'"
    + ", reachable_bbs:'" + extractNodesIds(node->getReachableBBs()) + "'"
    + ", dt_bbs_map:'" + extractBBsMapWithSet2String(&(node->getDomTreeMap())) + "'"
    + ", pdt_bbs_map:'" + extractBBsMapWithSet2String(&(node->getLoopAndDomInfo()->getPostDomTreeMap())) + "'"
    + ", df_bbs_map:'" + extractBBsMapWithSet2String(&(node->getDomFrontierMap())) + "'"
    + ", bb2_loop_map:'" + extractBBsMapWithSet2String(&(node->getLoopAndDomInfo()->getBB2LoopMap())) + "'"
    + ", bb2_p_dom_level:'" + extractLabelMap2String(&(node->getLoopAndDomInfo()->getBBPDomLevel())) + "'"
    + ", bb2_pi_dom:'" + extractBBsMap2String(&(node->getLoopAndDomInfo()->getBB2PIdom())) + "'"
    + "})";
    return queryStatement;
}

std::string GraphDBClient::generateSVFStmtEdgeFieldsStmt(const SVFStmt* edge)
{
    std::string valueStr = "";
    if (nullptr != edge->getValue())
    {
        valueStr += ", svf_var_node_id:"+ std::to_string(edge->getValue()->getId());
    }
    else
    {
        valueStr += ", svf_var_node_id:-1";
    }
    std::string bb_id_str = "";
    if (nullptr != edge->getBB())
    {
        bb_id_str += ", bb_id:" + std::to_string(edge->getBB()->getId());
    }
    else 
    {
        bb_id_str += ", bb_id:-1";
    }

    std::string icfg_node_id_str = "";
    if (nullptr != edge->getICFGNode())
    {
        icfg_node_id_str += ", icfg_node_id:" + std::to_string(edge->getICFGNode()->getId());
    }
    else 
    {
        icfg_node_id_str += ", icfg_node_id:-1";
    }

    std::string inst2_label_map = "";
    if (nullptr != edge->getInst2LabelMap() && !edge->getInst2LabelMap()->empty())
    {
        inst2_label_map += ", inst2_label_map:'"+ extractLabelMap2String(edge->getInst2LabelMap()) +"'";
    }

    std::string var2_label_map = "";
    if (nullptr != edge->getVar2LabelMap() && !edge->getVar2LabelMap()->empty())
    {
        var2_label_map += ", var2_label_map:'"+ extractLabelMap2String(edge->getVar2LabelMap()) +"'";
    }
    std::string fieldsStr = "";
    fieldsStr += "edge_id: " + std::to_string(edge->getEdgeID()) + 
    valueStr +
    bb_id_str +
    icfg_node_id_str +
    inst2_label_map +
    var2_label_map +
    ", call_edge_label_counter:" + std::to_string(*(edge->getCallEdgeLabelCounter())) +
    ", store_edge_label_counter:" + std::to_string(*(edge->getStoreEdgeLabelCounter())) +
    ", multi_opnd_label_counter:" + std::to_string(*(edge->getMultiOpndLabelCounter()));
    return fieldsStr;
}

std::string GraphDBClient::generateSVFStmtEdgeInsertStmt(const SVFStmt* edge)
{
    std::string srcKind = getPAGNodeKindString(edge->getSrcNode());
    std::string dstKind = getPAGNodeKindString(edge->getDstNode());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(edge->getDstNode()->getId()) +
        " CREATE (n)-[r:SVFStmt{"+
        generateSVFStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateAssignStmtFieldsStmt(const AssignStmt* edge)
{
    return generateSVFStmtEdgeFieldsStmt(edge);
}

std::string GraphDBClient::generateAssignStmtEdgeInsertStmt(const AssignStmt* edge)
{
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:AssignStmt{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateAddrStmtEdgeInsertStmt(const AddrStmt* edge)
{
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:AddrStmt{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", arr_size:'" + extractNodesIds(edge->getArrSize()) +"'"+
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateCopyStmtEdgeInsertStmt(const CopyStmt* edge)
{
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:CopyStmt{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", copy_kind:" + std::to_string(edge->getCopyKind()) +
        "}]->(m)";
    return queryStatement;   
}

std::string GraphDBClient::generateStoreStmtEdgeInsertStmt(const StoreStmt* edge)
{
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:StoreStmt{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateLoadStmtEdgeInsertStmt(const LoadStmt* edge)
{
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:LoadStmt{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateGepStmtEdgeInsertStmt(const GepStmt* edge)
{
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    std::ostringstream accessPathStr;
    accessPathStr << "";
    if (!edge->isVariantFieldGep())
    {
        accessPathStr << ", ap_fld_idx:"
                      << std::to_string(edge->getConstantStructFldIdx());
    }
    else
    {
        accessPathStr << ", ap_fld_idx:-1";
    }

    if (nullptr != edge->getAccessPath().gepSrcPointeeType())
    {
        accessPathStr << ", ap_gep_pointee_type_name:'"
                      << edge->getAccessPath().gepSrcPointeeType()->toString()
                      << "'";
    }
    if (!edge->getAccessPath().getIdxOperandPairVec().empty())
    {
        accessPathStr << ", ap_idx_operand_pairs:'"
                      << IdxOperandPairsToString(
                             &edge->getAccessPath().getIdxOperandPairVec())
                      << "'";
    }

    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:GepStmt{" + generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        accessPathStr.str() +
        ", variant_field:" + (edge->isVariantFieldGep()? "true" : "false") +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateCallPEEdgeInsertStmt(const CallPE* edge)
{
    std::string callInstStr = "";
    std::string funEntryICFGNodeStr = "";
    if (nullptr != edge->getCallInst()) 
    {
        callInstStr +=  ", call_icfg_node_id:" + std::to_string(edge->getCallInst()->getId());
    }
    else
    {
        callInstStr +=  ", call_icfg_node_id:-1";
    }

    if (nullptr != edge->getFunEntryICFGNode())
    {
        funEntryICFGNodeStr +=  ", fun_entry_icfg_node_id:" + std::to_string(edge->getFunEntryICFGNode()->getId());
    }
    else 
    {
        funEntryICFGNodeStr +=  ", fun_entry_icfg_node_id:-1";
    }
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:CallPE{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        callInstStr +
        funEntryICFGNodeStr +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateRetPEEdgeInsertStmt(const RetPE* edge)
{
    std::string callInstStr = "";
    std::string funExitICFGNodeStr = "";
    if (nullptr != edge->getCallInst()) 
    {
        callInstStr +=  ", call_icfg_node_id:" + std::to_string(edge->getCallInst()->getId());
    }
    else 
    {
        callInstStr +=  ", call_icfg_node_id:-1";
    }

    if (nullptr != edge->getFunExitICFGNode())
    {
        funExitICFGNodeStr +=  ", fun_exit_icfg_node_id:" + std::to_string(edge->getFunExitICFGNode()->getId());
    }
    else 
    {
        funExitICFGNodeStr +=  ", fun_exit_icfg_node_id:-1";
    }
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:RetPE{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        callInstStr +
        funExitICFGNodeStr +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateTDForkPEEdgeInsertStmt(const TDForkPE* edge)
{
    std::string callInstStr = "";
    std::string funEntryICFGNodeStr = "";
    if (nullptr != edge->getCallInst()) 
    {
        callInstStr +=  ", call_icfg_node_id:" + std::to_string(edge->getCallInst()->getId());
    }
    else 
    {
        callInstStr +=  ", call_icfg_node_id:-1";
    }

    if (nullptr != edge->getFunEntryICFGNode())
    {
        funEntryICFGNodeStr +=  ", fun_entry_icfg_node_id:" + std::to_string(edge->getFunEntryICFGNode()->getId());
    }
    else 
    {
        funEntryICFGNodeStr +=  ", fun_entry_icfg_node_id:-1";
    }
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:TDForkPE{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        callInstStr +
        funEntryICFGNodeStr +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateTDJoinPEEdgeInsertStmt(const TDJoinPE* edge)
{
    std::string callInstStr = "";
    std::string funExitICFGNodeStr = "";
    if (nullptr != edge->getCallInst()) 
    {
        callInstStr +=  ", call_icfg_node_id:" + std::to_string(edge->getCallInst()->getId());
    }
    else
    {
        callInstStr +=  ", call_icfg_node_id:-1";
    }

    if (nullptr != edge->getFunExitICFGNode())
    {
        funExitICFGNodeStr +=  ", fun_exit_icfg_node_id:" + std::to_string(edge->getFunExitICFGNode()->getId());
    }
    else 
    {
        funExitICFGNodeStr +=  ", fun_exit_icfg_node_id:-1";
    }
    std::string srcKind = getPAGNodeKindString(edge->getRHSVar());
    std::string dstKind = getPAGNodeKindString(edge->getLHSVar());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(edge->getRHSVar()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(edge->getLHSVar()->getId())+"}) WHERE n.id = " +
        std::to_string(edge->getRHSVar()->getId()) +
        " AND m.id = " + std::to_string(edge->getLHSVar()->getId()) +
        " CREATE (n)-[r:TDJoinPE{"+
        generateAssignStmtFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        callInstStr +
        funExitICFGNodeStr +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateMultiOpndStmtEdgeFieldsStmt(const MultiOpndStmt* edge)
{
    std::string stmt = generateSVFStmtEdgeFieldsStmt(edge);
    if (! edge->getOpndVars().empty())
    {
        stmt += ", op_var_node_ids:'" + extractNodesIds(edge->getOpndVars())+"'";
    }
    return stmt;
}

std::string GraphDBClient::generateMultiOpndStmtEdgeInsertStmt(const MultiOpndStmt* edge)
{
    const SVFStmt* stmt = SVFUtil::cast<SVFStmt>(edge); 
    std::string srcKind = getPAGNodeKindString(stmt->getSrcNode());
    std::string dstKind = getPAGNodeKindString(stmt->getDstNode());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(stmt->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(stmt->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(stmt->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(stmt->getDstNode()->getId()) +
        " CREATE (n)-[r:MultiOpndStmt{"+
        generateMultiOpndStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generatePhiStmtEdgeInsertStmt(const PhiStmt* edge)
{
    const SVFStmt* stmt = SVFUtil::cast<SVFStmt>(edge); 
    std::string srcKind = getPAGNodeKindString(stmt->getSrcNode());
    std::string dstKind = getPAGNodeKindString(stmt->getDstNode());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(stmt->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(stmt->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(stmt->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(stmt->getDstNode()->getId()) +
        " CREATE (n)-[r:PhiStmt{"+
        generateMultiOpndStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", op_icfg_nodes_ids:'" + extractNodesIds(*(edge->getOpICFGNodeVec())) + "'"+
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateSelectStmtEndgeInsertStmt(const SelectStmt* edge)
{
    const SVFStmt* stmt = SVFUtil::cast<SVFStmt>(edge); 
    std::string srcKind = getPAGNodeKindString(stmt->getSrcNode());
    std::string dstKind = getPAGNodeKindString(stmt->getDstNode());
    const std::string queryStatement =
       "MATCH (n:"+srcKind+"{id:"+std::to_string(stmt->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(stmt->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(stmt->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(stmt->getDstNode()->getId()) +
        " CREATE (n)-[r:SelectStmt{"+
        generateMultiOpndStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", condition_svf_var_node_id:" + std::to_string(edge->getCondition()->getId()) + 
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateCmpStmtEdgeInsertStmt(const CmpStmt* edge)
{
    const SVFStmt* stmt = SVFUtil::cast<SVFStmt>(edge); 
    std::string srcKind = getPAGNodeKindString(stmt->getSrcNode());
    std::string dstKind = getPAGNodeKindString(stmt->getDstNode());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(stmt->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(stmt->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(stmt->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(stmt->getDstNode()->getId()) +
        " CREATE (n)-[r:CmpStmt{"+
        generateMultiOpndStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", predicate:" + std::to_string(edge->getPredicate()) + 
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateBinaryOPStmtEdgeInsertStmt(const BinaryOPStmt* edge)
{
    const SVFStmt* stmt = SVFUtil::cast<SVFStmt>(edge); 
    std::string srcKind = getPAGNodeKindString(stmt->getSrcNode());
    std::string dstKind = getPAGNodeKindString(stmt->getDstNode());
    const std::string queryStatement =
       "MATCH (n:"+srcKind+"{id:"+std::to_string(stmt->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(stmt->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(stmt->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(stmt->getDstNode()->getId()) +
        " CREATE (n)-[r:BinaryOPStmt{"+
        generateMultiOpndStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", op_code:" + std::to_string(edge->getOpcode()) + 
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::genereateUnaryOPStmtEdgeInsertStmt(const UnaryOPStmt* edge)
{
    const SVFStmt* stmt = SVFUtil::cast<SVFStmt>(edge); 
    std::string srcKind = getPAGNodeKindString(stmt->getSrcNode());
    std::string dstKind = getPAGNodeKindString(stmt->getDstNode());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(stmt->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(stmt->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(stmt->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(stmt->getDstNode()->getId()) +
        " CREATE (n)-[r:UnaryOPStmt{"+
        generateSVFStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", op_code:" + std::to_string(edge->getOpcode()) + 
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::generateBranchStmtEdgeInsertStmt(const BranchStmt* edge)
{
    const SVFStmt* stmt = SVFUtil::cast<SVFStmt>(edge); 
    std::string srcKind = getPAGNodeKindString(stmt->getSrcNode());
    std::string dstKind = getPAGNodeKindString(stmt->getDstNode());
    const std::string queryStatement =
        "MATCH (n:"+srcKind+"{id:"+std::to_string(stmt->getSrcNode()->getId())+"}), (m:"+dstKind+"{id:"+std::to_string(stmt->getDstNode()->getId())+"}) WHERE n.id = " +
        std::to_string(stmt->getSrcNode()->getId()) +
        " AND m.id = " + std::to_string(stmt->getDstNode()->getId()) +
        " CREATE (n)-[r:BranchStmt{"+
        generateSVFStmtEdgeFieldsStmt(edge) +
        ", kind:" + std::to_string(edge->getEdgeKind()) +
        ", successors:'" + extractSuccessorsPairSet2String(&(edge->getSuccessors())) + "'"+ 
        ", condition_svf_var_node_id:" + std::to_string(edge->getCondition()->getId()) +
        ", br_inst_svf_var_node_id:" + std::to_string(edge->getBranchInst()->getId()) +
        "}]->(m)";
    return queryStatement;
}

std::string GraphDBClient::getPAGNodeKindString(const SVFVar* node)
{
    if(SVFUtil::isa<ConstNullPtrValVar>(node))
    {
        return "ConstNullPtrValVar";
    }
    else if(SVFUtil::isa<ConstIntValVar>(node))
    {
        return "ConstIntValVar";
    }
    else if(SVFUtil::isa<ConstFPValVar>(node))
    {
        return "ConstFPValVar";
    }
    else if(SVFUtil::isa<BlackHoleValVar>(node))
    {
        return "BlackHoleValVar";
    }
    else if(SVFUtil::isa<ConstDataValVar>(node))
    {
        return "ConstDataValVar";
    }
    else if(SVFUtil::isa<RetValPN>(node))
    {
        return "RetValPN";
    }
    else if(SVFUtil::isa<VarArgValPN>(node))
    {
        return "VarArgValPN";
    }
    else if(SVFUtil::isa<DummyValVar>(node))
    {
        return "DummyValVar";
    }
    else if(SVFUtil::isa<ConstAggValVar>(node))
    {
        return "ConstAggValVar";
    }
    else if(SVFUtil::isa<GlobalValVar>(node))
    {
        return "GlobalValVar";
    }
    else if(SVFUtil::isa<FunValVar>(node))
    {
        return "FunValVar";
    }
    else if(SVFUtil::isa<GepValVar>(node))
    {
        return "GepValVar";
    }
    else if(SVFUtil::isa<ArgValVar>(node))
    {
        return "ArgValVar";
    }
    else if(SVFUtil::isa<ValVar>(node))
    {
        return "ValVar";
    }
    else if(SVFUtil::isa<ConstNullPtrObjVar>(node))
    {
        return "ConstNullPtrObjVar";
    }
    else if(SVFUtil::isa<ConstIntObjVar>(node))
    {
        return "ConstIntObjVar";
    }
    else if(SVFUtil::isa<ConstFPObjVar>(node))
    {
        return "ConstFPObjVar";
    }
    else if(SVFUtil::isa<ConstDataObjVar>(node))
    {
        return "ConstDataObjVar";
    }
    else if(SVFUtil::isa<DummyObjVar>(node))
    {
        return "DummyObjVar";
    }
    else if(SVFUtil::isa<ConstAggObjVar>(node))
    {
        return "ConstAggObjVar";
    }
    else if(SVFUtil::isa<GlobalObjVar>(node))
    {
        return "GlobalObjVar";
    }
    else if(SVFUtil::isa<FunObjVar>(node))
    {
        return "FunObjVar";
    }
    else if(SVFUtil::isa<StackObjVar>(node))
    {
        return "StackObjVar";
    }
    else if(SVFUtil::isa<HeapObjVar>(node))
    {
        return "HeapObjVar";
    } 
    else if(SVFUtil::isa<BaseObjVar>(node))
    {
        return "BaseObjVar";
    }
    else if(SVFUtil::isa<GepObjVar>(node))
    {
        return "GepObjVar";
    }
    else if(SVFUtil::isa<ObjVar>(node))
    {
        return "ObjVar";
    }
   
        assert("unknown SVFVar node type?");
        return "SVFVar";
    
}

void GraphDBClient::readSVFTypesFromDB(lgraph::RpcClient* connection, const std::string& dbname, SVFIR* pag)
{
    SVFUtil::outs()<< "Build SVF types from DB....\n";
    addSVFTypeNodeFromDB(connection, dbname, pag);
}

void GraphDBClient::addSVFTypeNodeFromDB(lgraph::RpcClient* connection, const std::string& dbname, SVFIR* pag)
{
    // parse all SVFType
    std::string queryStatement = "MATCH (node) WHERE NOT 'StInfo' IN labels(node) return node";

    Map<std::string, SVFType*> svfTypeMap;
    Map<int, StInfo*> stInfoMap;
    // Map<SVFType::SVFTyKind, Set<SVFType*>> svfTypeKind2SVFTypesMap;
    Map<SVFType*, std::pair<std::string, std::string>> svfi8AndPtrTypeMap;
    Map<std::string, Set<SVFFunctionType*>> functionRetTypeSetMap;
    Map<SVFFunctionType*, Set<std::string>> functionParamsTypeSetMap;
    Map<int,Set<SVFType*>> stInfoId2SVFTypeMap;
    Map<std::string, Set<SVFArrayType*>> elementTyepsMap;
    
    cJSON* root = queryFromDB(connection, dbname, queryStatement);
    cJSON* node;
    if (nullptr != root)
    {
        cJSON_ArrayForEach(node, root)
        {
            cJSON* data = cJSON_GetObjectItem(node, "node");
            if (!data)
                continue;

            cJSON* properties = cJSON_GetObjectItem(data, "properties");
            if (!properties)
                continue;

            std::string label = cJSON_GetObjectItem(data, "label")->valuestring;

            SVFType* type = nullptr;
            std::string i8Type =
                cJSON_GetObjectItem(properties, "svf_i8_type_name")
                    ->valuestring;
            std::string ptrType =
                cJSON_GetObjectItem(properties, "svf_ptr_type_name")
                    ->valuestring;
            bool svt = cJSON_IsTrue(
                cJSON_GetObjectItem(properties, "is_single_val_ty"));
            int byteSize =
                cJSON_GetObjectItem(properties, "byte_size")->valueint;
            std::string typeNameString =
                cJSON_GetObjectItem(properties, "type_name")->valuestring;

            if (label == "SVFPointerType")
            {
                type = new SVFPointerType(byteSize, svt);
            }
            else if (label == "SVFIntegerType")
            {
                cJSON* single_and_width_Json =
                    cJSON_GetObjectItem(properties, "single_and_width");
                short single_and_width =
                    (short)cJSON_GetNumberValue(single_and_width_Json);
                type = new SVFIntegerType(byteSize, svt, single_and_width);
            }
            else if (label == "SVFFunctionType")
            {
                SVFFunctionType* funType = new SVFFunctionType(svt, byteSize);
                type = funType;
                std::string retTypeName =
                    cJSON_GetObjectItem(properties, "ret_ty_node_name")
                        ->valuestring;
                auto it = svfTypeMap.find(retTypeName);
                if (it != svfTypeMap.end())
                {
                    funType->setReturnType(it->second);
                }
                else
                {
                    functionRetTypeSetMap[retTypeName].insert(funType);
                }
                std::string paramsTypes =
                    cJSON_GetObjectItem(properties, "params_types_vec")
                        ->valuestring;
                if (!paramsTypes.empty())
                {
                    functionParamsTypeSetMap[funType] =
                        parseSVFTypes(paramsTypes);
                }
            }
            else if (label == "SVFOtherType")
            {
                std::string repr =
                    cJSON_GetObjectItem(properties, "repr")->valuestring;
                type = new SVFOtherType(svt, byteSize, repr);
            }
            else if (label == "SVFStructType")
            {
                std::string name =
                    cJSON_GetObjectItem(properties, "struct_name")->valuestring;
                type = new SVFStructType(svt, byteSize, name);
                int stInfoID =
                    cJSON_GetObjectItem(properties, "stinfo_node_id")->valueint;
                auto it = stInfoMap.find(stInfoID);
                if (it != stInfoMap.end())
                {
                    type->setTypeInfo(it->second);
                }
                else
                {
                    stInfoId2SVFTypeMap[stInfoID].insert(type);
                }
            }
            else if (label == "SVFArrayType")
            {
                int numOfElement =
                    cJSON_GetObjectItem(properties, "num_of_element")->valueint;
                SVFArrayType* arrayType =
                    new SVFArrayType(svt, byteSize, numOfElement);
                type = arrayType;
                int stInfoID =
                    cJSON_GetObjectItem(properties, "stinfo_node_id")->valueint;
                auto stInfoIter = stInfoMap.find(stInfoID);
                if (stInfoIter != stInfoMap.end())
                {
                    type->setTypeInfo(stInfoIter->second);
                }
                else
                {
                    stInfoId2SVFTypeMap[stInfoID].insert(type);
                }
                std::string typeOfElementName =
                    cJSON_GetObjectItem(properties,
                                        "type_of_element_node_type_name")
                        ->valuestring;
                auto tyepIter = svfTypeMap.find(typeOfElementName);
                if (tyepIter != svfTypeMap.end())
                {
                    arrayType->setTypeOfElement(tyepIter->second);
                }
                else
                {
                    elementTyepsMap[typeOfElementName].insert(arrayType);
                }
            }
            svfTypeMap.emplace(typeNameString, type);
            // svfTypeKind2SVFTypesMap[type->getSVFTyKind()].insert(type);
            svfi8AndPtrTypeMap[type] = std::make_pair(i8Type, ptrType);
        }
        cJSON_Delete(root);
    }

    // parse all StInfo
    queryStatement = "MATCH (node:StInfo) return node";
    root = queryFromDB(connection, dbname, queryStatement);
    if (nullptr != root)
    {
        cJSON_ArrayForEach(node, root)
        {
            cJSON* data = cJSON_GetObjectItem(node, "node");
            if (!data)
                continue;

            cJSON* properties = cJSON_GetObjectItem(data, "properties");
            if (!properties)
                continue;

            u32_t id = static_cast<u32_t>(
                cJSON_GetObjectItem(properties, "id")->valueint);
            std::string fld_idx_vec =
                cJSON_GetObjectItem(properties, "fld_idx_vec")->valuestring;
            std::vector<u32_t> fldIdxVec =
                parseElements2Container<std::vector<u32_t>>(fld_idx_vec);

            std::string elem_idx_vec =
                cJSON_GetObjectItem(properties, "elem_idx_vec")->valuestring;
            std::vector<u32_t> elemIdxVec =
                parseElements2Container<std::vector<u32_t>>(elem_idx_vec);

            std::string fld_idx_2_type_map =
                cJSON_GetObjectItem(properties, "fld_idx_2_type_map")
                    ->valuestring;
            Map<u32_t, const SVFType*> fldIdx2TypeMap =
                parseStringToFldIdx2TypeMap<Map<u32_t, const SVFType*>>(
                    fld_idx_2_type_map, svfTypeMap);

            std::string finfo_types =
                cJSON_GetObjectItem(properties, "finfo_types")->valuestring;
            std::vector<const SVFType*> finfo =
                parseElementsToSVFTypeContainer<std::vector<const SVFType*>>(
                    finfo_types, svfTypeMap);

            u32_t stride = static_cast<u32_t>(
                cJSON_GetObjectItem(properties, "stride")->valueint);
            u32_t num_of_flatten_elements = static_cast<u32_t>(
                cJSON_GetObjectItem(properties, "num_of_flatten_elements")
                    ->valueint);
            u32_t num_of_flatten_fields = static_cast<u32_t>(
                cJSON_GetObjectItem(properties, "num_of_flatten_fields")
                    ->valueint);
            std::string flatten_element_types =
                cJSON_GetObjectItem(properties, "flatten_element_types")
                    ->valuestring;
            std::vector<const SVFType*> flattenElementTypes =
                parseElementsToSVFTypeContainer<std::vector<const SVFType*>>(
                    flatten_element_types, svfTypeMap);
            StInfo* stInfo =
                new StInfo(id, fldIdxVec, elemIdxVec, fldIdx2TypeMap, finfo,
                           stride, num_of_flatten_elements,
                           num_of_flatten_fields, flattenElementTypes);
            stInfoMap[id] = stInfo;
        }
        cJSON_Delete(root);
    }

    for (auto& [retTypeName, types]:functionRetTypeSetMap)
    {
        auto retTypeIter = svfTypeMap.find(retTypeName);
        if (retTypeIter != svfTypeMap.end())
        {
            for (auto& type: types)
            {
                type->setReturnType(retTypeIter->second);
            }
        }
        else
        {
            SVFUtil::outs()
                << "Warning3: No matching SVFType found for type: " << retTypeName << "\n";
        }
    }
    Set<const SVFType*> ori = pag->getSVFTypes();

    for (auto& [funType, paramsSet]:functionParamsTypeSetMap)
    {
        for (std::string param : paramsSet)
        {
            if (!param.empty() && param.front() == '{')
            {
                param.erase(0, 1); 
            }
            if (!param.empty() && param.back() == '}')
            {
                param.erase(param.size() - 1, 1); 
            }
            auto paramTypeIter = svfTypeMap.find(param);
            if (paramTypeIter != svfTypeMap.end())
            {
                funType->addParamType(paramTypeIter->second);
            } 
            else
            {
                SVFUtil::outs()<<"Warning2: No matching SVFType found for type: "
                              << param << "\n";
            }
        }
    }

    for (auto&[stInfoId, types] : stInfoId2SVFTypeMap)
    {
        auto stInfoIter = stInfoMap.find(stInfoId);
        if (stInfoIter != stInfoMap.end())
        {
            for (SVFType* type: types)
            {
                type->setTypeInfo(stInfoIter->second);
            }
        }
        else
        {
            SVFUtil::outs()<<"Warning: No matching StInfo found for id: "
            << stInfoId << "\n";
        }
    }

    for (auto& [elementTypesName, arrayTypes]:elementTyepsMap)
    {
        auto elementTypeIter = svfTypeMap.find(elementTypesName);
        if (elementTypeIter != svfTypeMap.end())
        {
            for (SVFArrayType* type : arrayTypes)
            {
                type->setTypeOfElement(elementTypeIter->second);
            }
        }
        else 
        {
            SVFUtil::outs()<<"Warning1: No matching SVFType found for type: "
            << elementTypesName << "\n";
        }
    }

    for (auto& [svfType, pair]:svfi8AndPtrTypeMap)
    {
        std::string svfi8Type = pair.first;
        std::string svfptrType = pair.second;
        auto i8Type = svfTypeMap.find(svfi8Type);
        auto ptrType = svfTypeMap.find(svfptrType);
        if (i8Type!=svfTypeMap.end())
        {
            svfType->setSVFInt8Type(i8Type->second);
        }
        if (ptrType!= svfTypeMap.end())
        {
            svfType->setSVFPtrType(ptrType->second);
        }
    }
    for (auto& [typeName, type] : svfTypeMap)
    {
        pag->addTypeInfo(type);
    }
    for (auto& [id, stInfo]: stInfoMap)
    {
        pag->addStInfo(stInfo);
    }
}

void GraphDBClient::initialSVFPAGNodesFromDB(lgraph::RpcClient* connection, const std::string& dbname, SVFIR* pag)
{
    SVFUtil::outs()<< "Initial SVF PAG nodes from DB....\n";
    readPAGNodesFromDB(connection, dbname, "ValVar", pag);
    readPAGNodesFromDB(connection, dbname, "ObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "ArgValVar", pag);
    readPAGNodesFromDB(connection, dbname, "GepValVar", pag);
    readPAGNodesFromDB(connection, dbname, "BaseObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "GepObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "HeapObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "StackObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "FunObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "FunValVar", pag);
    readPAGNodesFromDB(connection, dbname, "GlobalValVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstAggValVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstDataValVar", pag);
    readPAGNodesFromDB(connection, dbname, "BlackHoleValVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstFPValVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstIntValVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstNullPtrValVar", pag);
    readPAGNodesFromDB(connection, dbname, "GlobalObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstAggObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstDataObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstFPObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstIntObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "ConstNullPtrObjVar", pag);
    readPAGNodesFromDB(connection, dbname, "RetValPN", pag);
    readPAGNodesFromDB(connection, dbname, "VarArgValPN", pag);
    readPAGNodesFromDB(connection, dbname, "DummyValVar", pag);
    readPAGNodesFromDB(connection, dbname, "DummyObjVar", pag);
}


void GraphDBClient::readPAGNodesFromDB(lgraph::RpcClient* connection, const std::string& dbname, std::string nodeType, SVFIR* pag)
{
    std::string result;
    std::string queryStatement = " MATCH (node:"+nodeType+") RETURN node";
    cJSON* root = queryFromDB(connection, dbname, queryStatement);
    if (nullptr != root)
    {
        cJSON* node;
        cJSON_ArrayForEach(node, root)
        {
            cJSON* data = cJSON_GetObjectItem(node, "node");
            if (!data)
                continue;
            cJSON* properties = cJSON_GetObjectItem(data, "properties");
            if (!properties)
                continue;
            int id = cJSON_GetObjectItem(properties,"id")->valueint;
            std::string svfTypeName = cJSON_GetObjectItem(properties, "svf_type_name")->valuestring;
            const SVFType* type = pag->getSVFType(svfTypeName);
            if (nodeType == "ConstNullPtrValVar")
            {
                ConstNullPtrValVar* var = new ConstNullPtrValVar(id, type, ValVar::ConstNullptrValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "ConstIntValVar")
            {
                u64_t zval = std::stoull(cJSON_GetObjectItem(properties, "zval")->valuestring);
                s64_t sval = cJSON_GetObjectItem(properties, "sval")->valueint;
                ConstIntValVar* var = new ConstIntValVar(id, sval, zval, type, ValVar::ConstIntValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "ConstFPValVar")
            {
                double dval = cJSON_GetObjectItem(properties, "dval")->valuedouble;
                ConstFPValVar* var = new ConstFPValVar(id, dval, type, ValVar::ConstFPValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "ArgValVar")
            {
                u32_t arg_no = static_cast<u32_t>(cJSON_GetObjectItem(properties, "arg_no")->valueint);
                ArgValVar* var = new ArgValVar(id, type,arg_no, ValVar::ArgValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "BlackHoleValVar")
            {
                BlackHoleValVar* var = new BlackHoleValVar(id, type, ValVar::BlackHoleValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "ConstDataValVar")
            {
                ConstDataValVar* var = new ConstDataValVar(id, type, ValVar::ConstDataValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "RetValPN")
            {
                RetValPN* var = new RetValPN(id, type, ValVar::RetValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "VarArgValPN")
            {
                VarArgValPN* var = new VarArgValPN(id, type, ValVar::VarargValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "DummyValVar")
            {
                DummyValVar* var = new DummyValVar(id, type, ValVar::DummyValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "ConstAggValVar")
            {
                ConstAggValVar* var = new ConstAggValVar(id, type, ValVar::ConstAggValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "GlobalValVar")
            {
                GlobalValVar* var = new GlobalValVar(id, type, ValVar::GlobalValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "FunValVar")
            {
                FunValVar* var = new FunValVar(id, type, ValVar::FunValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "GepValVar")
            {
                std::string gep_val_svf_type_name = cJSON_GetObjectItem(properties, "gep_val_svf_type_name")->valuestring;
                const SVFType* gepValType = pag->getSVFType(gep_val_svf_type_name);
                GepValVar* var = new GepValVar(id, type, gepValType, ValVar::GepValNode);
                pag->addInitValNode(var);
            }
            else if (nodeType == "ValVar")
            {
                ValVar* var = new ValVar(id, type, ValVar::ValNode);
                pag->addValNodeFromDB(var);
            }
            else if (nodeType == "ConstNullPtrObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                ConstNullPtrObjVar* var = new ConstNullPtrObjVar(id, type, objTypeInfo, ObjVar::ConstNullptrObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "ConstIntObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                u64_t zval = std::stoull(cJSON_GetObjectItem(properties, "zval")->valuestring);
                s64_t sval = cJSON_GetObjectItem(properties, "sval")->valueint;
                ConstIntObjVar* var = new ConstIntObjVar(id, sval, zval, type, objTypeInfo, ObjVar::ConstIntObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "ConstFPObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                float dval = (float)(cJSON_GetObjectItem(properties, "dval")->valuedouble);
                ConstFPObjVar* var = new ConstFPObjVar(id, dval, type, objTypeInfo, ObjVar::ConstFPObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "ConstDataObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                ConstDataObjVar* var = new ConstDataObjVar(id, type, objTypeInfo, ObjVar::ConstDataObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "DummyObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                DummyObjVar* var = new DummyObjVar(id, type, objTypeInfo, ObjVar::DummyObjNode);
                pag->addDummyObjNode(var);
            }
            else if (nodeType == "ConstAggObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                ConstAggObjVar* var = new ConstAggObjVar(id, type, objTypeInfo, ObjVar::ConstAggObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "GlobalObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                GlobalObjVar* var = new GlobalObjVar(id, type, objTypeInfo, ObjVar::GlobalObjNode);
                pag->addBaseObjNode(var); 
            }
            else if (nodeType == "FunObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                bool is_decl = cJSON_IsTrue(cJSON_GetObjectItem(properties, "is_decl"));
                bool intrinsic = cJSON_IsTrue(cJSON_GetObjectItem(properties, "intrinsic"));
                bool is_addr_taken = cJSON_IsTrue(cJSON_GetObjectItem(properties, "is_addr_taken"));
                bool is_uncalled = cJSON_IsTrue(cJSON_GetObjectItem(properties, "is_uncalled"));
                bool is_not_return = cJSON_IsTrue(cJSON_GetObjectItem(properties, "is_not_return"));
                bool sup_var_arg = cJSON_IsTrue(cJSON_GetObjectItem(properties, "sup_var_arg"));
                std::string fun_type_name = cJSON_GetObjectItem(properties, "fun_type_name")->valuestring;
                const SVFFunctionType* funcType = SVFUtil::dyn_cast<SVFFunctionType>(pag->getSVFType(fun_type_name));
                FunObjVar* var = new FunObjVar(id, type, objTypeInfo, is_decl, intrinsic, is_addr_taken, is_uncalled, is_not_return, sup_var_arg, funcType, ObjVar::FunObjNode);
                pag->addBaseObjNode(var);
                id2funObjVarsMap[id] = var;                
            }
            else if (nodeType == "StackObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                StackObjVar* var = new StackObjVar(id, type, objTypeInfo, ObjVar::StackObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "HeapObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                HeapObjVar* var = new HeapObjVar(id, type, objTypeInfo, ObjVar::HeapObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "BaseObjVar")
            {
                ObjTypeInfo* objTypeInfo = parseObjTypeInfoFromDB(properties, pag);
                BaseObjVar* var = new BaseObjVar(id, type, objTypeInfo, ObjVar::BaseObjNode);
                pag->addBaseObjNode(var);
            }
            else if (nodeType == "GepObjVar")
            {
                s64_t app_offset = cJSON_GetObjectItem(properties, "app_offset")->valueint;
                int base_obj_var_node_id = cJSON_GetObjectItem(properties, "base_obj_var_node_id")->valueint;
                const BaseObjVar* baseObj = pag->getBaseObject(base_obj_var_node_id);
                GepObjVar* var = new GepObjVar(id, type, app_offset, baseObj, ObjVar::GepObjNode);
                pag->addGepObjNode(var);
            }
            else if (nodeType == "ObjVar")
            {
                ObjVar* var = new ObjVar(id, type, ObjVar::ObjNode);
                pag->addObjNodeFromDB(SVFUtil::cast<ObjVar>(var));
            }
        }
        cJSON_Delete(root);
    }
}

ObjTypeInfo* GraphDBClient::parseObjTypeInfoFromDB(cJSON* properties, SVFIR* pag)
{
    std::string obj_type_info_type_name = cJSON_GetObjectItem(properties, "obj_type_info_type_name")->valuestring;
    const SVFType* objTypeInfoType = pag->getSVFType(obj_type_info_type_name);
    int obj_type_info_flags = cJSON_GetObjectItem(properties, "obj_type_info_flags")->valueint;
    int obj_type_info_max_offset_limit = cJSON_GetObjectItem(properties, "obj_type_info_max_offset_limit")->valueint;
    int obj_type_info_elem_num = cJSON_GetObjectItem(properties, "obj_type_info_elem_num")->valueint;
    int obj_type_info_byte_size = cJSON_GetObjectItem(properties, "obj_type_info_byte_size")->valueint;
    ObjTypeInfo* objTypeInfo = new ObjTypeInfo(objTypeInfoType, obj_type_info_flags, obj_type_info_max_offset_limit, obj_type_info_elem_num, obj_type_info_byte_size);
    if (nullptr != objTypeInfo)
        return objTypeInfo;
    return nullptr;
}

cJSON* GraphDBClient::queryFromDB(lgraph::RpcClient* connection, const std::string& dbname, std::string queryStatement)
{
    // parse all SVFType
    std::string result;
    if (!connection->CallCypher(result, queryStatement, dbname))
    {
        SVFUtil::outs() << queryStatement<< "\n";
        SVFUtil::outs() << "Failed to query from DB:" << result << "\n";
        return nullptr;
    } 
    cJSON* root = cJSON_Parse(result.c_str());
    if (!root || !cJSON_IsArray(root))
    {
        SVFUtil::outs() << "Invalid JSON format: "<<queryStatement<<"\n";
        cJSON_Delete(root);
        return nullptr;
    }

    return root;
}

void GraphDBClient::readBasicBlockGraphFromDB(lgraph::RpcClient* connection, const std::string& dbname)
{
    SVFUtil::outs()<< "Build BasicBlockGraph from DB....\n";
    for (auto& item : id2funObjVarsMap)
    {
        FunObjVar* funObjVar = item.second;
        readBasicBlockNodesFromDB(connection, dbname, funObjVar);
    }

    for (auto& item : id2funObjVarsMap)
    {
        FunObjVar* funObjVar = item.second;
        readBasicBlockEdgesFromDB(connection, dbname, funObjVar);
    }
}

void GraphDBClient::readBasicBlockNodesFromDB(lgraph::RpcClient* connection, const std::string& dbname, FunObjVar* funObjVar)
{
    NodeID id = funObjVar->getId();
    std::string queryStatement ="MATCH (node) where node.fun_obj_var_id = " + std::to_string(id) +" RETURN node";
    cJSON* root = queryFromDB(connection, dbname, queryStatement);
    if (nullptr != root)
    {
        cJSON* node;
        BasicBlockGraph* bbGraph = new BasicBlockGraph();
        funObjVar->setBasicBlockGraph(bbGraph);
        cJSON_ArrayForEach(node, root)
        {
            cJSON* data = cJSON_GetObjectItem(node, "node");
            if (!data)
                continue;
            cJSON* properties = cJSON_GetObjectItem(data, "properties");
            if (!properties)
                continue;
            std::string id = cJSON_GetObjectItem(properties, "id")->valuestring;
            std::string bb_name =
                cJSON_GetObjectItem(properties, "bb_name")->valuestring;
            int bbId = parseBBId(id);
            SVFBasicBlock* bb = new SVFBasicBlock(bbId, funObjVar);
            bb->setName(bb_name);
            bbGraph->addBasicBlock(bb);
            basicBlocks.insert(bb);
        }
        cJSON_Delete(root);
    }
}

void GraphDBClient::readBasicBlockEdgesFromDB(lgraph::RpcClient* connection, const std::string& dbname, FunObjVar* funObjVar)
{
    BasicBlockGraph* bbGraph = funObjVar->getBasicBlockGraph();
    if (nullptr != bbGraph)
    {
        for (auto& pair: *bbGraph)
        {
            SVFBasicBlock* bb = pair.second;
            std::string queryStatement = "MATCH (node{id:'"+std::to_string(bb->getId())+":"+std::to_string(bb->getFunction()->getId())+"'}) RETURN node.pred_bb_ids, node.sscc_bb_ids";
            cJSON* root = queryFromDB(connection, dbname, queryStatement);
            if (nullptr != root)
            {
                cJSON* item;
                cJSON_ArrayForEach(item, root)
                {
                    if (!item)
                        continue;
                    std::string pred_bb_ids = cJSON_GetObjectItem(item, "node.pred_bb_ids")->valuestring;
                    std::string sscc_bb_ids = cJSON_GetObjectItem(item, "node.sscc_bb_ids")->valuestring;
                    if (!pred_bb_ids.empty())
                    {
                        std::vector<int> predBBIds = parseElements2Container<std::vector<int>>(pred_bb_ids);
                        for (int predBBId : predBBIds)
                        {

                            SVFBasicBlock* predBB = bbGraph->getGNode(predBBId);
                            if (nullptr != predBB)
                            {
                                bb->addPredBasicBlock(predBB);
                            }

                        }
                    }
                    if (!sscc_bb_ids.empty())
                    {
                        std::vector<int> ssccBBIds = parseElements2Container<std::vector<int>>(sscc_bb_ids);
                        for (int ssccBBId : ssccBBIds)
                        {
                            SVFBasicBlock* ssccBB = bbGraph->getGNode(ssccBBId);
                            if (nullptr != ssccBB)
                            {
                                bb->addSuccBasicBlock(ssccBB);
                            }

                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
    }
}

ICFG* GraphDBClient::buildICFGFromDB(lgraph::RpcClient* connection, const std::string& dbname, SVFIR* pag)
{
    SVFUtil::outs()<< "Build ICFG from DB....\n";
    DBOUT(DGENERAL, outs() << pasMsg("\t Building ICFG From DB ...\n"));
    ICFG* icfg = new ICFG();
    // read & add all the ICFG nodes from DB
    readICFGNodesFromDB(connection, dbname, "GlobalICFGNode", icfg, pag);
    readICFGNodesFromDB(connection, dbname, "FunEntryICFGNode", icfg, pag);
    readICFGNodesFromDB(connection, dbname, "FunExitICFGNode", icfg, pag);
    readICFGNodesFromDB(connection, dbname, "IntraICFGNode", icfg, pag);
    // need to parse the RetICFGNode first before parsing the CallICFGNode
    readICFGNodesFromDB(connection, dbname, "RetICFGNode", icfg, pag);
    readICFGNodesFromDB(connection, dbname, "CallICFGNode", icfg, pag);

    // read & add all the ICFG edges from DB
    readICFGEdgesFromDB(connection, dbname, "IntraCFGEdge", icfg, pag);
    readICFGEdgesFromDB(connection, dbname, "CallCFGEdge", icfg, pag);
    readICFGEdgesFromDB(connection, dbname, "RetCFGEdge", icfg, pag);

    return icfg;
}

void GraphDBClient::readICFGNodesFromDB(lgraph::RpcClient* connection, const std::string& dbname, std::string nodeType, ICFG* icfg, SVFIR* pag)
{
    std::string queryStatement = " MATCH (node:"+nodeType+") RETURN node";
    cJSON* root = queryFromDB(connection, dbname, queryStatement);
    if (nullptr != root)
    {
        cJSON* node;
        cJSON_ArrayForEach(node, root)
        {
            ICFGNode* icfgNode = nullptr;
            if (nodeType == "GlobalICFGNode")
            {
                icfgNode = parseGlobalICFGNodeFromDBResult(node);
                if (nullptr != icfgNode)
                {
                    icfg->addGlobalICFGNode(SVFUtil::cast<GlobalICFGNode>(icfgNode));
                }
            }
            else if (nodeType == "IntraICFGNode")
            {
                icfgNode = parseIntraICFGNodeFromDBResult(node, pag);
                if (nullptr != icfgNode)
                {
                    icfg->addIntraICFGNode(SVFUtil::cast<IntraICFGNode>(icfgNode));
                }
            }
            else if (nodeType == "FunEntryICFGNode")
            {
                icfgNode = parseFunEntryICFGNodeFromDBResult(node, pag);
                if (nullptr != icfgNode)
                {
                    icfg->addFunEntryICFGNode(SVFUtil::cast<FunEntryICFGNode>(icfgNode));
                }
            }
            else if (nodeType == "FunExitICFGNode")
            {
                icfgNode = parseFunExitICFGNodeFromDBResult(node, pag);
                if (nullptr != icfgNode)
                {
                    icfg->addFunExitICFGNode(SVFUtil::cast<FunExitICFGNode>(icfgNode));
                }
            }
            else if (nodeType == "RetICFGNode")
            {
                icfgNode = parseRetICFGNodeFromDBResult(node, pag);
                if (nullptr != icfgNode)
                {
                    icfg->addRetICFGNode(SVFUtil::cast<RetICFGNode>(icfgNode));
                    id2RetICFGNodeMap[icfgNode->getId()] = SVFUtil::cast<RetICFGNode>(icfgNode);
                }
            }
            else if (nodeType == "CallICFGNode")
            {
                icfgNode = parseCallICFGNodeFromDBResult(node, pag);
                if (nullptr != icfgNode)
                {
                    icfg->addCallICFGNode(SVFUtil::cast<CallICFGNode>(icfgNode));
                }
            }
            
            if (nullptr == icfgNode)
            {
                SVFUtil::outs()<< "Failed to create "<< nodeType<< " from db query result\n";
            }
        }
        cJSON_Delete(root);
    }
}

ICFGNode* GraphDBClient::parseGlobalICFGNodeFromDBResult(const cJSON* node)
{
    cJSON* data = cJSON_GetObjectItem(node, "node");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;

    GlobalICFGNode* icfgNode;
    int id = cJSON_GetObjectItem(properties,"id")->valueint;

    icfgNode = new GlobalICFGNode(id);
    return icfgNode;
}

ICFGNode* GraphDBClient::parseFunEntryICFGNodeFromDBResult(const cJSON* node, SVFIR* pag)
{
    cJSON* data = cJSON_GetObjectItem(node, "node");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;

    FunEntryICFGNode* icfgNode;
    int id = cJSON_GetObjectItem(properties,"id")->valueint;
    int fun_obj_var_id = cJSON_GetObjectItem(properties, "fun_obj_var_id")->valueint; 
    FunObjVar* funObjVar = nullptr;
    auto funObjVarIt = id2funObjVarsMap.find(fun_obj_var_id);
    if (funObjVarIt != id2funObjVarsMap.end())
    {
        funObjVar = funObjVarIt->second;
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseFunEntryICFGNodeFromDBResult] No matching FunObjVar found for id: " << fun_obj_var_id << "\n";
    }

    // parse FunEntryICFGNode bb
    int bb_id = cJSON_GetObjectItem(properties, "bb_id")->valueint;
    SVFBasicBlock* bb = funObjVar->getBasicBlockGraph()->getGNode(bb_id);

    icfgNode = new FunEntryICFGNode(id, funObjVar, bb);
    std::string fpNodesStr = cJSON_GetObjectItem(properties, "fp_nodes")->valuestring;
    std::vector<u32_t> fpNodesIdVec = parseElements2Container<std::vector<u32_t>>(fpNodesStr);
    for (auto fpNodeId: fpNodesIdVec)
    {
        SVFVar* fpNode = pag->getGNode(fpNodeId);
        if (nullptr != fpNode)
        {
            icfgNode->addFormalParms(fpNode);
        }
        else 
        {
            SVFUtil::outs() << "Warning: [parseFunEntryICFGNodeFromDBResult] No matching fpNode SVFVar found for id: " << fpNodeId << "\n";
        }
    }

    if (nullptr != bb)
    {
        bb->addICFGNode(icfgNode);
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseFunEntryICFGNodeFromDBResult] No matching BasicBlock found for id: " << bb_id << "\n";
    }


    return icfgNode;
}

ICFGNode* GraphDBClient::parseFunExitICFGNodeFromDBResult(const cJSON* node, SVFIR* pag)
{
    cJSON* data = cJSON_GetObjectItem(node, "node");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;

    FunExitICFGNode* icfgNode;
    int id = cJSON_GetObjectItem(properties,"id")->valueint;

    int fun_obj_var_id = cJSON_GetObjectItem(properties, "fun_obj_var_id")->valueint;
    FunObjVar* funObjVar = nullptr;
    auto funObjVarIt = id2funObjVarsMap.find(fun_obj_var_id);
    if (funObjVarIt != id2funObjVarsMap.end())
    {
        funObjVar = funObjVarIt->second;
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseFunExitICFGNodeFromDBResult] No matching FunObjVar found for id: " << fun_obj_var_id << "\n";
    }

    // parse FunExitICFGNode bb
    int bb_id = cJSON_GetObjectItem(properties, "bb_id")->valueint;
    SVFBasicBlock* bb = funObjVar->getBasicBlockGraph()->getGNode(bb_id);

    icfgNode = new FunExitICFGNode(id, funObjVar, bb);
    int formal_ret_node_id = cJSON_GetObjectItem(properties, "formal_ret_node_id")->valueint;
    if (formal_ret_node_id != -1)
    {
        SVFVar* formalRet = pag->getGNode(formal_ret_node_id);
        if (nullptr != formalRet)
        {
            icfgNode->addFormalRet(formalRet);
        }
        else
        {
            SVFUtil::outs() << "Warning: [parseFunExitICFGNodeFromDBResult] No matching formalRet SVFVar found for id: " << formal_ret_node_id << "\n";
        }
    }

    if (nullptr != bb)
    {
        bb->addICFGNode(icfgNode);
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseFunExitICFGNodeFromDBResult] No matching BasicBlock found for id: " << bb_id << "\n";
    }

    return icfgNode;
}

ICFGNode* GraphDBClient::parseIntraICFGNodeFromDBResult(const cJSON* node, SVFIR* pag)
{
    cJSON* data = cJSON_GetObjectItem(node, "node");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;

    IntraICFGNode* icfgNode;
    int id = cJSON_GetObjectItem(properties, "id")->valueint;
    // parse intraICFGNode funObjVar
    int fun_obj_var_id = cJSON_GetObjectItem(properties, "fun_obj_var_id")->valueint;
    FunObjVar* funObjVar = nullptr;
    auto funObjVarIt = id2funObjVarsMap.find(fun_obj_var_id);
    if (funObjVarIt != id2funObjVarsMap.end())
    {
        funObjVar = funObjVarIt->second;
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseIntraICFGNodeFromDBResult] No matching FunObjVar found for id: " << fun_obj_var_id << "\n";
    }

    // parse intraICFGNode bb
    int bb_id = cJSON_GetObjectItem(properties, "bb_id")->valueint;
    SVFBasicBlock* bb = funObjVar->getBasicBlockGraph()->getGNode(bb_id);

    // parse isRet 
    bool is_return = cJSON_IsTrue(cJSON_GetObjectItem(properties, "is_return"));

    
    icfgNode = new IntraICFGNode(id, bb, funObjVar, is_return);

    // add this ICFGNode to its BasicBlock
    if (nullptr != bb)
    {
        bb->addICFGNode(icfgNode);
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseIntraICFGNodeFromDBResult] No matching BasicBlock found for id: " << bb_id << "\n";
    }
    return icfgNode;
}

ICFGNode* GraphDBClient::parseRetICFGNodeFromDBResult(const cJSON* node, SVFIR* pag)
{
    cJSON* data = cJSON_GetObjectItem(node, "node");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;
    
    RetICFGNode* icfgNode;
    // parse retICFGNode id
    int id = cJSON_GetObjectItem(properties, "id")->valueint;

    // parse retICFGNode funObjVar
    int fun_obj_var_id = cJSON_GetObjectItem(properties, "fun_obj_var_id")->valueint;
    FunObjVar* funObjVar = nullptr;
    auto funObjVarIt = id2funObjVarsMap.find(fun_obj_var_id);
    if (funObjVarIt != id2funObjVarsMap.end())
    {
        funObjVar = funObjVarIt->second;
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseRetICFGNodeFromDBResult] No matching FunObjVar found for id: " << fun_obj_var_id << "\n";
    }

    // parse retICFGNode bb
    int bb_id = cJSON_GetObjectItem(properties, "bb_id")->valueint;
    SVFBasicBlock* bb = funObjVar->getBasicBlockGraph()->getGNode(bb_id);

    // parse retICFGNode svfType
    std::string svfTypeName = cJSON_GetObjectItem(properties, "svf_type")->valuestring;
    const SVFType* type = pag->getSVFType(svfTypeName);
    if (nullptr == type)
    {
        SVFUtil::outs() << "Warning: [parseRetICFGNodeFromDBResult] No matching SVFType found for: " << svfTypeName << "\n";
    }

    // create RetICFGNode Instance 
    icfgNode = new RetICFGNode(id, type, bb, funObjVar);

    // parse & add actualRet for RetICFGNode
    int actual_ret_node_id = cJSON_GetObjectItem(properties, "actual_ret_node_id")->valueint;
    if (actual_ret_node_id != -1)
    {
        SVFVar* actualRet = pag->getGNode(actual_ret_node_id);
        if (nullptr != actualRet)
        {
            icfgNode->addActualRet(actualRet);
        }
        else
        {
            SVFUtil::outs() << "Warning: [parseRetICFGNodeFromDBResult] No matching actualRet SVFVar found for id: " << actual_ret_node_id << "\n";
        }
    }

    // add this ICFGNode to its BasicBlock
    if (nullptr != bb)
    {
        bb->addICFGNode(icfgNode);
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseRetICFGNodeFromDBResult] No matching BasicBlock found for id: " << bb_id << "\n";
    }
    return icfgNode;
}

ICFGNode* GraphDBClient::parseCallICFGNodeFromDBResult(const cJSON* node, SVFIR* pag)
{
    cJSON* data = cJSON_GetObjectItem(node, "node");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;
    
    CallICFGNode* icfgNode;

    // parse CallICFGNode id
    int id = cJSON_GetObjectItem(properties, "id")->valueint;

    // parse CallICFGNode funObjVar
    int fun_obj_var_id = cJSON_GetObjectItem(properties, "fun_obj_var_id")->valueint;
    FunObjVar* funObjVar = nullptr;
    auto funObjVarIt = id2funObjVarsMap.find(fun_obj_var_id);
    if (funObjVarIt != id2funObjVarsMap.end())
    {
        funObjVar = funObjVarIt->second;
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseCallICFGNodeFromDBResult] No matching FunObjVar found for id: " << fun_obj_var_id << "\n";
    }

    // parse CallICFGNode bb
    int bb_id = cJSON_GetObjectItem(properties, "bb_id")->valueint;
    SVFBasicBlock* bb = funObjVar->getBasicBlockGraph()->getGNode(bb_id);

    // parse CallICFGNode svfType
    std::string svfTypeName = cJSON_GetObjectItem(properties, "svf_type")->valuestring;
    const SVFType* type = pag->getSVFType(svfTypeName);
    if (nullptr == type)
    {
        SVFUtil::outs() << "Warning: [parseCallICFGNodeFromDBResult] No matching SVFType found for: " << svfTypeName << "\n";
    }

    // parse CallICFGNode calledFunObjVar
    int called_fun_obj_var_id = cJSON_GetObjectItem(properties, "called_fun_obj_var_id")->valueint;
    FunObjVar* calledFunc = nullptr;
    if (called_fun_obj_var_id != -1)
    {
        auto calledFuncIt = id2funObjVarsMap.find(called_fun_obj_var_id);
        if (calledFuncIt != id2funObjVarsMap.end())
        {
            calledFunc = calledFuncIt->second;
        }
        else
        {
            SVFUtil::outs() << "Warning: [parseCallICFGNodeFromDBResult] No matching calledFunObjVar found for id: " << called_fun_obj_var_id << "\n";
        }
    }

    bool is_vararg = cJSON_IsTrue(cJSON_GetObjectItem(properties, "is_vararg"));
    bool is_vir_call_inst = cJSON_IsTrue(cJSON_GetObjectItem(properties, "is_vir_call_inst"));

    // parse CallICFGNode retICFGNode
    int ret_icfg_node_id = cJSON_GetObjectItem(properties, "ret_icfg_node_id")->valueint;
    RetICFGNode* retICFGNode = nullptr;
    if (ret_icfg_node_id != -1)
    {
        auto retICFGNodeIt = id2RetICFGNodeMap.find(ret_icfg_node_id);
        if (retICFGNodeIt != id2RetICFGNodeMap.end())
        {
            retICFGNode = retICFGNodeIt->second;
        }
        else
        {
            SVFUtil::outs() << "Warning: [parseCallICFGNodeFromDBResult] No matching RetICFGNode found for id: " << ret_icfg_node_id << "\n";
        }
    }
    
    std::string fun_name_of_v_call = "";
    s32_t virtualFunIdx = 0;
    SVFVar* vtabPtr = nullptr;
    if (is_vir_call_inst)
    {
        int virtual_fun_idx = cJSON_GetObjectItem(properties, "virtual_fun_idx")->valueint;
        virtualFunIdx = static_cast<s32_t>(virtual_fun_idx);
        int vtab_ptr_node_id = cJSON_GetObjectItem(properties, "vtab_ptr_node_id")->valueint;
        vtabPtr = pag->getGNode(vtab_ptr_node_id);
        fun_name_of_v_call = cJSON_GetObjectItem(properties, "fun_name_of_v_call")->valuestring;
    }
     
    // create CallICFGNode Instance
    icfgNode = new CallICFGNode(id, bb, type, funObjVar, calledFunc, retICFGNode,
        is_vararg, is_vir_call_inst, virtualFunIdx, vtabPtr, fun_name_of_v_call);
    
    // parse CallICFGNode APNodes
    std::string ap_nodes = cJSON_GetObjectItem(properties, "ap_nodes")->valuestring;
    if (!ap_nodes.empty() && ap_nodes!= "[]")
    {
        std::vector<u32_t> apNodesIdVec = parseElements2Container<std::vector<u32_t>>(ap_nodes);
        if (apNodesIdVec.size() > 0)
        {
            for (auto apNodeId: apNodesIdVec)
            {
                SVFVar* apNode = pag->getGNode(apNodeId);
                if (nullptr != apNode)
                {
                    icfgNode->addActualParms(SVFUtil::cast<ValVar>(apNode));
                }
                else
                {
                    SVFUtil::outs() << "Warning: [parseCallICFGNodeFromDBResult] No matching APNode ValVar found for id: " << apNodeId << "\n";
                }
            }
        }
    }

    if (retICFGNode != nullptr)
    {
        retICFGNode->addCallBlockNode(icfgNode);
    }

    // add this ICFGNode to its BasicBlock
    if (nullptr != bb)
    {
        bb->addICFGNode(icfgNode);
    }
    else
    {
        SVFUtil::outs() << "Warning: [parseCallICFGNodeFromDBResult] No matching BasicBlock found for id: " << bb_id << "\n";
    }
    
    return icfgNode;
}

void GraphDBClient::readICFGEdgesFromDB(lgraph::RpcClient* connection, const std::string& dbname, std::string edgeType, ICFG* icfg, SVFIR* pag)
{
    std::string queryStatement =  "MATCH ()-[edge:"+edgeType+"]->() RETURN edge";
    cJSON* root = queryFromDB(connection, dbname, queryStatement);
    if (nullptr != root)
    {
        cJSON* edge;
        cJSON_ArrayForEach(edge, root)
        {
            ICFGEdge* icfgEdge = nullptr;
            if (edgeType == "IntraCFGEdge")
            {
                icfgEdge = parseIntraCFGEdgeFromDBResult(edge, pag, icfg);
            }
            else if (edgeType == "CallCFGEdge")
            {
                icfgEdge = parseCallCFGEdgeFromDBResult(edge, pag, icfg);
                
            }
            else if (edgeType == "RetCFGEdge")
            {
                icfgEdge = parseRetCFGEdgeFromDBResult(edge, pag, icfg);
            }
            if (nullptr != icfgEdge)
            {
                icfg->addICFGEdge(icfgEdge);
            }
            else 
            {
                SVFUtil::outs()<< "Failed to create "<< edgeType << " from db query result\n";
            }
        }
        cJSON_Delete(root);
    }
}

ICFGEdge* GraphDBClient::parseIntraCFGEdgeFromDBResult(const cJSON* edge, SVFIR* pag, ICFG* icfg)
{
    cJSON* data = cJSON_GetObjectItem(edge, "edge");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;

    IntraCFGEdge* icfgEdge;

    // parse srcICFGNode & dstICFGNode
    int src_id = cJSON_GetObjectItem(data,"src")->valueint;
    int dst_id = cJSON_GetObjectItem(data,"dst")->valueint; 
    ICFGNode* src = icfg->getICFGNode(src_id);
    ICFGNode* dst = icfg->getICFGNode(dst_id);

    if (src == nullptr)
    {
        SVFUtil::outs() << "Warning: [parseIntraCFGEdgeFromDBResult] No matching src ICFGNode found for id: " << src_id << "\n";
        return nullptr;
    }
    if (dst == nullptr)
    {
        SVFUtil::outs() << "Warning: [parseIntraCFGEdgeFromDBResult] No matching dst ICFGNode found for id: " << dst_id << "\n";
        return nullptr;
    }

    // create IntraCFGEdge Instance
    icfgEdge = new IntraCFGEdge(src, dst);
   
    // parse branchCondVal & conditionalVar
    int condition_var_id = cJSON_GetObjectItem(properties, "condition_var_id")->valueint;
    int branch_cond_val = cJSON_GetObjectItem(properties, "branch_cond_val")->valueint;
    s64_t branchCondVal = 0;
    SVFVar* conditionVar;
    if (condition_var_id != -1 && branch_cond_val != -1)
    {
        branchCondVal = static_cast<s64_t>(branch_cond_val);
        conditionVar = pag->getGNode(condition_var_id);
        if (nullptr == conditionVar)
        {
            SVFUtil::outs() << "Warning: [parseIntraCFGEdgeFromDBResult] No matching conditionVar found for id: " << condition_var_id << "\n";
        }
        icfgEdge->setConditionVar(conditionVar);
        icfgEdge->setBranchCondVal(branchCondVal);
    }

    return icfgEdge;
}

ICFGEdge* GraphDBClient::parseCallCFGEdgeFromDBResult(const cJSON* edge, SVFIR* pag, ICFG* icfg)
{
    cJSON* data = cJSON_GetObjectItem(edge, "edge");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;

    CallCFGEdge* icfgEdge;
    // parse srcICFGNode & dstICFGNode
    int src_id = cJSON_GetObjectItem(data,"src")->valueint;
    int dst_id = cJSON_GetObjectItem(data,"dst")->valueint; 
    ICFGNode* src = icfg->getICFGNode(src_id);
    ICFGNode* dst = icfg->getICFGNode(dst_id);
    if (src == nullptr)
    {
        SVFUtil::outs() << "Warning: [parseCallCFGEdgeFromDBResult] No matching src ICFGNode found for id: " << src_id << "\n";
        return nullptr;
    }
    if (dst == nullptr)
    {
        SVFUtil::outs() << "Warning: [parseCallCFGEdgeFromDBResult] No matching dst ICFGNode found for id: " << dst_id << "\n";
        return nullptr;
    }

    // create CallCFGEdge Instance
    icfgEdge = new CallCFGEdge(src, dst);

    return icfgEdge;
}

ICFGEdge* GraphDBClient::parseRetCFGEdgeFromDBResult(const cJSON* edge, SVFIR* pag, ICFG* icfg)
{
    cJSON* data = cJSON_GetObjectItem(edge, "edge");
    if (!data)
        return nullptr;

    cJSON* properties = cJSON_GetObjectItem(data, "properties");
    if (!properties)
        return nullptr;
    
    RetCFGEdge* icfgEdge;
    // parse srcICFGNode & dstICFGNode
    int src_id = cJSON_GetObjectItem(data,"src")->valueint;
    int dst_id = cJSON_GetObjectItem(data,"dst")->valueint; 
    ICFGNode* src = icfg->getICFGNode(src_id);
    ICFGNode* dst = icfg->getICFGNode(dst_id);
    if (src == nullptr)
    {
        SVFUtil::outs() << "Warning: [parseRetCFGEdgeFromDBResult] No matching src ICFGNode found for id: " << src_id << "\n";
        return nullptr;
    }
    if (dst == nullptr)
    {
        SVFUtil::outs() << "Warning: [parseRetCFGEdgeFromDBResult] No matching dst ICFGNode found for id: " << dst_id << "\n";
        return nullptr;
    }

    // create RetCFGEdge Instance
    icfgEdge = new RetCFGEdge(src, dst);

    return icfgEdge;
}