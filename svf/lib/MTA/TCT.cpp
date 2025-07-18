//===- TCT.cpp -- Thread creation tree-------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013->  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//


/*
 * TCT.cpp
 *
 *  Created on: Jun 24, 2015
 *      Author: Yulei Sui, Peng Di
 */

#include "Util/Options.h"
#include "MTA/TCT.h"
#include "MTA/MTA.h"
#include "Graphs/CallGraph.h"

#include <string>

using namespace SVF;
using namespace SVFUtil;

/*!
 * An instruction i is in loop
 * (1) the instruction i itself
 * (2) all the callsites invoke the function where i resides in
 */
bool TCT::isInLoopInstruction(const ICFGNode* inst)
{
    assert(inst && "null value instruction!!");

    Set<const ICFGNode*> insts;
    FIFOWorkList<const ICFGNode*> worklist;
    worklist.push(inst);

    while(!worklist.empty())
    {
        const ICFGNode* inst = worklist.pop();
        insts.insert(inst);
        CallGraphNode* cgnode = tcg->getCallGraphNode(inst->getFun());
        for(CallGraphNode::const_iterator nit = cgnode->InEdgeBegin(), neit = cgnode->InEdgeEnd(); nit!=neit; nit++)
        {
            for(CallGraphEdge::CallInstSet::const_iterator cit = (*nit)->directCallsBegin(),
                    ecit = (*nit)->directCallsEnd(); cit!=ecit; ++cit)
            {
                if(insts.insert(*cit).second)
                    worklist.push(*cit);
            }
            for(CallGraphEdge::CallInstSet::const_iterator cit = (*nit)->indirectCallsBegin(),
                    ecit = (*nit)->indirectCallsEnd(); cit!=ecit; ++cit)
            {
                if(insts.insert(*cit).second)
                    worklist.push(*cit);
            }
        }
    }

    for(const ICFGNode* i : insts)
    {
        if(i->getFun()->hasLoopInfo(i->getBB()))
            return true;
    }


    return false;
}

/*!
 * An instruction i is in a recursion
 * (1) the function f where i resides in is in a recursion
 * (2) any caller function starting from the function f in is in a recursion
 */
bool TCT::isInRecursion(const ICFGNode* inst) const
{
    const FunObjVar* f = inst->getFun();
    FIFOWorkList<const FunObjVar*> worklist;
    Set<const FunObjVar*> visits;
    worklist.push(f);

    while(!worklist.empty())
    {
        const FunObjVar* svffun = worklist.pop();
        visits.insert(svffun);
        if(tcgSCC->isInCycle(tcg->getCallGraphNode(svffun)->getId()))
            return true;

        const CallGraphNode* cgnode = tcg->getCallGraphNode(svffun);

        for(CallGraphNode::const_iterator nit = cgnode->InEdgeBegin(), neit = cgnode->InEdgeEnd(); nit!=neit; nit++)
        {
            for(CallGraphEdge::CallInstSet::const_iterator cit = (*nit)->directCallsBegin(),
                    ecit = (*nit)->directCallsEnd(); cit!=ecit; ++cit)
            {
                const FunObjVar* caller = (*cit)->getFun();
                if(visits.find(caller)==visits.end())
                    worklist.push(caller);
            }
            for(CallGraphEdge::CallInstSet::const_iterator cit = (*nit)->indirectCallsBegin(),
                    ecit = (*nit)->indirectCallsEnd(); cit!=ecit; ++cit)
            {
                const FunObjVar* caller = (*cit)->getFun();
                if(visits.find(caller)==visits.end())
                    worklist.push(caller);
            }
        }
    }

    return false;

}

/*!
 * Mark relevant procedures that are backward reachable from any fork/join site
 */
void TCT::markRelProcs()
{
    for (ThreadCallGraph::CallSiteSet::const_iterator it = tcg->forksitesBegin(), eit = tcg->forksitesEnd(); it != eit; ++it)
    {
        const FunObjVar* svfun = (*it)->getParent()->getParent();
        markRelProcs(svfun);

        for(ThreadCallGraph::ForkEdgeSet::const_iterator nit = tcg->getForkEdgeBegin(*it), neit = tcg->getForkEdgeEnd(*it); nit!=neit; nit++)
        {
            const CallGraphNode* forkeeNode = (*nit)->getDstNode();
            candidateFuncSet.insert(forkeeNode->getFunction());
        }

    }

    for (ThreadCallGraph::CallSiteSet::const_iterator it = tcg->joinsitesBegin(), eit = tcg->joinsitesEnd(); it != eit; ++it)
    {
        const FunObjVar* svfun = (*it)->getParent()->getParent();
        markRelProcs(svfun);
    }

    if(candidateFuncSet.empty())
        writeWrnMsg("We didn't recognize any fork site, this is single thread program?");
}

/*!
 *
 */
void TCT::markRelProcs(const FunObjVar* svffun)
{
    CallGraphNode* cgnode = tcg->getCallGraphNode(svffun);
    FIFOWorkList<const CallGraphNode*> worklist;
    PTACGNodeSet visited;
    worklist.push(cgnode);
    visited.insert(cgnode);
    while(!worklist.empty())
    {
        const CallGraphNode* node = worklist.pop();
        candidateFuncSet.insert(node->getFunction());
        for(CallGraphNode::const_iterator nit = node->InEdgeBegin(), neit = node->InEdgeEnd(); nit!=neit; nit++)
        {
            const CallGraphNode* srcNode = (*nit)->getSrcNode();
            if(visited.find(srcNode)==visited.end())
            {
                visited.insert(srcNode);
                worklist.push(srcNode);
            }
        }
    }
}

/*!
 * Get Main function
 */
void TCT::collectEntryFunInCallGraph()
{
    const CallGraph* svfirCallGraph = PAG::getPAG()->getCallGraph();
    for (const auto& item: *svfirCallGraph)
    {
        const FunObjVar* fun = item.second->getFunction();
        if (SVFUtil::isExtCall(fun))
            continue;
        CallGraphNode* node = tcg->getCallGraphNode(fun);
        if (!node->hasIncomingEdge())
        {
            entryFuncSet.insert(fun);
        }
    }
    assert(!entryFuncSet.empty() && "Can't find any function in module!");
}

/*!
 * Collect all multi-forked threads
 */
void TCT::collectMultiForkedThreads()
{
    if (this->nodeNum == 0 )
        return;

    FIFOWorkList<TCTNode*> worklist;
    worklist.push(getTCTNode(0));

    while(!worklist.empty())
    {
        TCTNode* node = worklist.pop();
        const CxtThread &ct = node->getCxtThread();

        if(ct.isIncycle() || ct.isInloop())
        {
            node->setMultiforked(true);
        }
        else
        {
            for (TCT::ThreadCreateEdgeSet::const_iterator it = node->getInEdges().begin(), eit = node->getInEdges().end(); it != eit;
                    ++it)
            {
                if ((*it)->getSrcNode()->isMultiforked())
                    node->setMultiforked(true);
            }
        }
        for (TCT::ThreadCreateEdgeSet::const_iterator it = node->getOutEdges().begin(), eit = node->getOutEdges().end(); it != eit;
                ++it)
        {
            worklist.push((*it)->getDstNode());
        }
    }
}


/*!
 * Handle call relations
 */
void TCT::handleCallRelation(CxtThreadProc& ctp, const CallGraphEdge* cgEdge, const CallICFGNode* cs)
{
    const FunObjVar* callee = cgEdge->getDstNode()->getFunction();

    CallStrCxt cxt(ctp.getContext());
    CallStrCxt oldCxt = cxt;
    const CallICFGNode* callNode = cs;
    pushCxt(cxt,callNode,callee);

    if(cgEdge->getEdgeKind() == CallGraphEdge::CallRetEdge)
    {
        CxtThreadProc newctp(ctp.getTid(),cxt,callee);
        if(pushToCTPWorkList(newctp))
        {
            DBOUT(DMTA,outs() << "TCT Process CallRet old ctp --"; ctp.dump());
            DBOUT(DMTA,outs() << "TCT Process CallRet new ctp --"; newctp.dump());
        }
    }

    else if(cgEdge->getEdgeKind() == CallGraphEdge::TDForkEdge)
    {
        /// Create spawnee TCT node
        TCTNode* spawneeNode = getOrCreateTCTNode(cxt,callNode, oldCxt, callee);
        CxtThreadProc newctp(spawneeNode->getId(),cxt,callee);

        if(pushToCTPWorkList(newctp))
        {
            /// Add TCT nodes and edge
            if(addTCTEdge(this->getGNode(ctp.getTid()), spawneeNode))
            {
                DBOUT(DMTA,outs() << "Add TCT Edge from thread " << ctp.getTid() << "  ";
                      this->getGNode(ctp.getTid())->getCxtThread().dump();
                      outs() << " to thread " << spawneeNode->getId() << "  ";
                      spawneeNode->getCxtThread().dump();
                      outs() << "\n" );
            }
            DBOUT(DMTA,outs() << "TCT Process Fork old ctp --"; ctp.dump());
            DBOUT(DMTA,outs() << "TCT Process Fork new ctp --"; newctp.dump());
        }
    }
}

/*!
 * Return true if a join instruction must be executed inside a loop
 * joinbb should post dominate the successive basic block of a loop header
 */
bool TCT::isJoinMustExecutedInLoop(const LoopBBs& lp,const ICFGNode* join)
{
    assert(!lp.empty() && "this is not a loop, empty basic block");
    const FunObjVar* svffun = join->getFun();
    const SVFBasicBlock* loopheadbb = svffun->getLoopHeader(lp);
    const SVFBasicBlock* joinbb = join->getBB();
    assert(loopheadbb->getParent()==joinbb->getParent() && "should inside same function");

    for (const SVFBasicBlock* svf_scc_bb : loopheadbb->getSuccessors())
    {
        if(svffun->loopContainsBB(lp,svf_scc_bb))
        {
            if(svffun->dominate(joinbb,svf_scc_bb)==false)
                return false;
        }
    }

    return true;
}

/*!
 * Collect loop info for join sites
 * the in-loop join site must be joined if the loop is executed
 */
void TCT::collectLoopInfoForJoin()
{
    for(ThreadCallGraph::CallSiteSet::const_iterator it = tcg->joinsitesBegin(), eit = tcg->joinsitesEnd(); it!=eit; ++it)
    {
        const ICFGNode* join = *it;
        const FunObjVar* svffun = join->getFun();
        const SVFBasicBlock* svfbb = join->getBB();

        if(svffun->hasLoopInfo(svfbb))
        {
            const LoopBBs& lp = svffun->getLoopInfo(svfbb);
            if(!lp.empty() && isJoinMustExecutedInLoop(lp,join))
            {
                joinSiteToLoopMap[join] = lp;
            }
        }

        if(isInRecursion(join))
            inRecurJoinSites.insert(join);
    }
}

/*!
 * Return true if a given bb is a loop head of a inloop join site
 */
bool TCT::isLoopHeaderOfJoinLoop(const SVFBasicBlock* bb)
{
    for(InstToLoopMap::const_iterator it = joinSiteToLoopMap.begin(), eit = joinSiteToLoopMap.end(); it!=eit; ++it)
    {
        if(bb->getParent()->getLoopHeader(it->second) == bb)
            return true;
    }

    return false;
}

/*!
 * Whether a given bb is an exit of a inloop join site
 */
bool TCT::isLoopExitOfJoinLoop(const SVFBasicBlock* bb)
{
    for(InstToLoopMap::const_iterator it = joinSiteToLoopMap.begin(), eit = joinSiteToLoopMap.end(); it!=eit; ++it)
    {
        std::vector<const SVFBasicBlock*> exitbbs;
        it->first->getFun()->getExitBlocksOfLoop(it->first->getBB(),exitbbs);
        while(!exitbbs.empty())
        {
            const SVFBasicBlock* eb = exitbbs.back();
            exitbbs.pop_back();
            if(eb == bb)
                return true;
        }
    }

    return false;
}

/*!
 * Get loop for fork/join site
 */
const TCT::LoopBBs& TCT::getLoop(const SVFBasicBlock* bb)
{
    const FunObjVar* fun = bb->getParent();
    return fun->getLoopInfo(bb);
}

/*!
 * Start building TCT
 */
void TCT::build()
{

    markRelProcs();

    collectLoopInfoForJoin();

    // the fork site of main function is initialized with nullptr.
    // the context of main is initialized with empty
    // start routine is empty

    collectEntryFunInCallGraph();
    for (FunSet::iterator it=entryFuncSet.begin(), eit=entryFuncSet.end(); it!=eit; ++it)
    {
        if (!isCandidateFun(*it))
            continue;
        CallStrCxt cxt;
        TCTNode* mainTCTNode = getOrCreateTCTNode(cxt, nullptr, cxt, *it);
        CxtThreadProc t(mainTCTNode->getId(), cxt, *it);
        pushToCTPWorkList(t);
    }

    while(!ctpList.empty())
    {
        CxtThreadProc ctp = popFromCTPWorkList();
        CallGraphNode* cgNode = tcg->getCallGraphNode(ctp.getProc());
        if(isCandidateFun(cgNode->getFunction()) == false)
            continue;

        for(CallGraphNode::const_iterator nit = cgNode->OutEdgeBegin(), neit = cgNode->OutEdgeEnd(); nit!=neit; nit++)
        {
            const CallGraphEdge* cgEdge = (*nit);

            for(CallGraphEdge::CallInstSet::const_iterator cit = cgEdge->directCallsBegin(),
                    ecit = cgEdge->directCallsEnd(); cit!=ecit; ++cit)
            {
                DBOUT(DMTA,outs() << "\nTCT handling direct call:" << **cit << "\t" << cgEdge->getSrcNode()->getFunction()->getName() << "-->" << cgEdge->getDstNode()->getFunction()->getName() << "\n");
                handleCallRelation(ctp,cgEdge,*cit);
            }
            for(CallGraphEdge::CallInstSet::const_iterator ind = cgEdge->indirectCallsBegin(),
                    eind = cgEdge->indirectCallsEnd(); ind!=eind; ++ind)
            {
                DBOUT(DMTA,outs() << "\nTCT handling indirect call:" << **ind << "\t" << cgEdge->getSrcNode()->getFunction()->getName() << "-->" << cgEdge->getDstNode()->getFunction()->getName() << "\n");
                handleCallRelation(ctp,cgEdge,*ind);
            }
        }
    }

    collectMultiForkedThreads();

    if (Options::TCTDotGraph())
    {
        print();
        dump("tct");
    }

}

/*!
 * Push calling context
 */
void TCT::pushCxt(CallStrCxt& cxt, const CallICFGNode* call, const FunObjVar* callee)
{

    const FunObjVar* caller = call->getFun();
    CallSiteID csId = tcg->getCallSiteID(call, callee);

    /// handle calling context for candidate functions only
    if(isCandidateFun(caller) == false)
        return;

    if(inSameCallGraphSCC(tcg->getCallGraphNode(caller),tcg->getCallGraphNode(callee))==false)
    {
        pushCxt(cxt,csId);
        DBOUT(DMTA,dumpCxt(cxt));
    }
}


/*!
 * Match calling context
 */
bool TCT::matchCxt(CallStrCxt& cxt, const CallICFGNode* call, const FunObjVar* callee)
{

    const FunObjVar* caller = call->getFun();
    CallSiteID csId = tcg->getCallSiteID(call, callee);

    /// handle calling context for candidate functions only
    if(isCandidateFun(caller) == false)
        return true;

    /// partial match
    if(cxt.empty())
        return true;

    if(inSameCallGraphSCC(tcg->getCallGraphNode(caller),tcg->getCallGraphNode(callee))==false)
    {
        if(cxt.back() == csId)
            cxt.pop_back();
        else
            return false;
        DBOUT(DMTA,dumpCxt(cxt));
    }

    return true;
}


/*!
 * Dump calling context information
 */
void TCT::dumpCxt(CallStrCxt& cxt)
{
    std::string str;
    std::stringstream rawstr(str);
    rawstr << "[:";
    for(CallStrCxt::const_iterator it = cxt.begin(), eit = cxt.end(); it!=eit; ++it)
    {
        rawstr << " ' "<< *it << " ' ";
        rawstr << (tcg->getCallSite(*it))->valueOnlyToString();
        rawstr << "  call  " << tcg->getCallSite(*it)->getCaller()->getName() << "-->" << tcg->getCalleeOfCallSite(*it)->getName() << ", \n";
    }
    rawstr << " ]";
    outs() << "max cxt = " << cxt.size() << rawstr.str() << "\n";
}

/*!
 * Dump call graph into dot file
 */
void TCT::dump(const std::string& filename)
{
    if (Options::TCTDotGraph())
        GraphPrinter::WriteGraphToFile(outs(), filename, this);
}

/*!
 * Print TCT information
 */
void TCT::print() const
{
    for(TCT::const_iterator it = this->begin(), eit = this->end(); it!=eit; ++it)
    {
        outs() << "TID " << it->first << "\t";
        it->second->getCxtThread().dump();
    }
}

/*!
 *  Whether we have already created this call graph edge
 */
TCTEdge* TCT::hasGraphEdge(TCTNode* src, TCTNode* dst, TCTEdge::CEDGEK kind) const
{
    TCTEdge edge(src, dst, kind);
    TCTEdge* outEdge = src->hasOutgoingEdge(&edge);
    TCTEdge* inEdge = dst->hasIncomingEdge(&edge);
    if (outEdge && inEdge)
    {
        assert(outEdge == inEdge && "edges not match");
        return outEdge;
    }
    else
        return nullptr;
}

/*!
 * get PTACallGraph edge via nodes
 */
TCTEdge* TCT::getGraphEdge(TCTNode* src, TCTNode* dst, TCTEdge::CEDGEK kind)
{
    for (TCTEdge::ThreadCreateEdgeSet::const_iterator iter = src->OutEdgeBegin(); iter != src->OutEdgeEnd(); ++iter)
    {
        TCTEdge* edge = (*iter);
        if (edge->getEdgeKind() == kind && edge->getDstID() == dst->getId())
            return edge;
    }
    return nullptr;
}
namespace SVF
{

/*!
 * Write value flow graph into dot file for debugging
 */
template<>
struct DOTGraphTraits<TCT*> : public DefaultDOTGraphTraits
{

    typedef TCTNode NodeType;
    typedef NodeType::iterator ChildIteratorType;
    DOTGraphTraits(bool isSimple = false) :
        DefaultDOTGraphTraits(isSimple)
    {
    }

    /// Return name of the graph
    static std::string getGraphName(TCT *graph)
    {
        return "Thread Create Tree";
    }
    /// Return function name;
    static std::string getNodeLabel(TCTNode *node, TCT *graph)
    {
        return std::to_string(node->getId());
    }

    static std::string getNodeAttributes(TCTNode *node, TCT *tct)
    {
        std::string attr;
        if (node->isInloop())
        {
            attr.append(" style=filled fillcolor=red");
        }
        else if (node->isIncycle())
        {
            attr.append(" style=filled fillcolor=yellow");
        }
        return attr;
    }

    template<class EdgeIter>
    static std::string getEdgeAttributes(TCTNode *node, EdgeIter EI, TCT *csThreadTree)
    {

        TCTEdge* edge = csThreadTree->getGraphEdge(node, *EI, TCTEdge::ThreadCreateEdge);
        (void)edge; // Suppress warning of unused variable under release build
        assert(edge && "No edge found!!");
        /// black edge for direct call while two functions contain indirect calls will be label with red color
        return "color=black";
    }
};
} // End namespace llvm

