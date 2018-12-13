#include "SPRSearch.h"
#include "Arguments.hpp"
#include <treeSearch/JointTree.h>
#include <treeSearch/Moves.h>
#include <treeSearch/SearchUtils.hpp>
#include <Logger.hpp>
#include <ParallelContext.hpp>



struct SPRMoveDesc {
  SPRMoveDesc(int prune, int regraft, const vector<int> &edges):
    pruneIndex(prune), regraftIndex(regraft), path(edges) {}
  int pruneIndex;
  int regraftIndex;
  vector<int> path;
};

void queryPruneIndicesRec(pll_unode_t * node,
                               vector<int> &buffer)
{
  if (node->next) {
    queryPruneIndicesRec(node->next->back, buffer);
    queryPruneIndicesRec(node->next->next->back, buffer);
    buffer.push_back(node->node_index);
    if (node->back->next) {
      buffer.push_back(node->back->node_index);
    }
  }
}

void getAllPruneIndices(JointTree &tree, vector<int> &allNodeIndices) {
  auto treeinfo = tree.getTreeInfo();
  for (int i = 0; i < treeinfo->subnode_count; ++i) {
    if (treeinfo->subnodes[i]->next) {
      allNodeIndices.push_back(treeinfo->subnodes[i]->node_index);
    }
  }
}


bool sprYeldsSameTree(pll_unode_t *p, pll_unode_t *r)
{
  return (r == p) || (r == p->next) || (r == p->next->next)
    || (r == p->back) || (r == p->next->back) || (r == p->next->next->back);
}

bool isValidSPRMove(shared_ptr<pllmod_treeinfo_t> treeinfo, pll_unode_s *prune, pll_unode_s *regraft) {
  return !sprYeldsSameTree(prune, regraft);
}



void getRegraftsRec(int pruneIndex, pll_unode_t *regraft, int maxRadius, vector<int> &path, vector<SPRMoveDesc> &moves)
{
  if (path.size()) {
    moves.push_back(SPRMoveDesc(pruneIndex, regraft->node_index, path));
  }
  if (path.size() < maxRadius && regraft->next) {
    path.push_back(regraft->node_index);
    getRegraftsRec(pruneIndex, regraft->next->back, maxRadius, path, moves);
    getRegraftsRec(pruneIndex, regraft->next->next->back, maxRadius, path, moves);
    path.pop_back();
  }
}


void printPossibleMoves(JointTree &jointTree, vector<shared_ptr<Move> > &allMoves)
{
  Logger::info << "Nodes: " << endl;
  jointTree.printAllNodes(cout);
  Logger::info << "Possible moves from " << jointTree.getUnrootedTreeHash() << endl;
  for (auto move: allMoves) {
    jointTree.applyMove(move);
    cerr << *move << " "  << jointTree.getUnrootedTreeHash() << endl;
    jointTree.rollbackLastMove();
  }
}

void getRegrafts(JointTree &jointTree, int pruneIndex, int maxRadius, vector<SPRMoveDesc> &moves) 
{
  pll_unode_t *pruneNode = jointTree.getNode(pruneIndex);
  vector<int> path;
  getRegraftsRec(pruneIndex, pruneNode->next->back, maxRadius, path, moves);
  getRegraftsRec(pruneIndex, pruneNode->next->next->back, maxRadius, path, moves);
}

bool SPRSearch::applySPRRound(JointTree &jointTree, int radius, double &bestLoglk) {
  shared_ptr<Move> bestMove(0);
  vector<int> allNodes;
  getAllPruneIndices(jointTree, allNodes);
  const size_t edgesNumber = jointTree.getTreeInfo()->tree->edge_count;
 
  vector<SPRMoveDesc> potentialMoves;
  vector<shared_ptr<Move> > allMoves;
  for (int i = 0; i < allNodes.size(); ++i) {
      int pruneIndex = allNodes[i];
      getRegrafts(jointTree, pruneIndex, radius, potentialMoves);
  }
  for (auto &move: potentialMoves) {
    int pruneIndex = move.pruneIndex;
    int regraftIndex = move.regraftIndex;
    if (!isValidSPRMove(jointTree.getTreeInfo(), jointTree.getNode(pruneIndex), jointTree.getNode(regraftIndex))) {
      continue;
    }
    allMoves.push_back(Move::createSPRMove(pruneIndex, regraftIndex, move.path));
  }
  //printPossibleMoves(jointTree, allMoves);
  
  Logger::timed << "Start SPR round " 
    << "(hash=" << jointTree.getUnrootedTreeHash() << ", (best ll=" << bestLoglk << ", radius=" << radius << ", possible moves: " << allMoves.size() << ")"
    << endl;
  int bestMoveIndex = -1;
  bool foundBetterMove = SearchUtils::findBestMove(jointTree, allMoves, bestLoglk, bestMoveIndex); 
  if (foundBetterMove) {
    jointTree.applyMove(allMoves[bestMoveIndex]);
  }
  return foundBetterMove;
}


void SPRSearch::applySPRSearch(JointTree &jointTree)
{ 
  jointTree.printLoglk();
  double startingLoglk = jointTree.computeJointLoglk();
  double bestLoglk = startingLoglk;

  while (applySPRRound(jointTree, 1, bestLoglk)) {}
  jointTree.optimizeParameters();
  bestLoglk = jointTree.computeJointLoglk();
  while (applySPRRound(jointTree, 1, bestLoglk)) {}
  jointTree.optimizeParameters();
  bestLoglk = jointTree.computeJointLoglk();
  while (applySPRRound(jointTree, 2, bestLoglk)) {}
  jointTree.optimizeParameters();
  bestLoglk = jointTree.computeJointLoglk();
  while (applySPRRound(jointTree, 5, bestLoglk)) {}
  //while (applySPRRound(jointTree, 50, bestLoglk)) {}
}

