#include "SearchUtils.hpp"
#include <trees/JointTree.hpp>
#include <ParallelContext.hpp>
#include <IO/Logger.hpp>



void SearchUtils::testMove(JointTree &jointTree,
    std::shared_ptr<Move> move,
    double initialReconciliationLoglk,
    double initialLibpllLoglk,
    double &averageReconciliationDiff,
    double &newLoglk,
    bool blo,
    bool check
    )
{
  double initialLoglk = initialReconciliationLoglk + initialLibpllLoglk;
  jointTree.applyMove(move); 
  double recLoglk = jointTree.computeReconciliationLoglk();
  double improvement = recLoglk - initialReconciliationLoglk;
  averageReconciliationDiff *= 50;
  averageReconciliationDiff += improvement;
  averageReconciliationDiff /= 51;
  if (improvement < averageReconciliationDiff) {
    jointTree.rollbackLastMove();
    if(check && fabs(initialLoglk - jointTree.computeJointLoglk()) > 0.000001) {
      std::cerr.precision(17);
      std::cerr << "small rollback lead to different likelihoods: " << initialLoglk
        << " " << jointTree.computeJointLoglk() << std::endl;
      std::cerr << " rank " << ParallelContext::getRank() << std::endl;
      std::cerr << "Move: " << *move << std::endl;
      exit(1);
    }
    return;
  }
  if (blo) {
    jointTree.optimizeMove(move);
  }
  newLoglk = recLoglk +  jointTree.computeLibpllLoglk(false);
  jointTree.rollbackLastMove();
  if(check) {
    auto rbLoglk = jointTree.computeJointLoglk();
    if (fabs(initialLoglk - rbLoglk) > 0.000001) {
      jointTree.printLoglk();
      std::cerr.precision(17);
      std::cerr << "rollback lead to different likelihoods: " << initialLoglk
        << " " << rbLoglk << std::endl;
      std::cerr << "recomputing the ll again: " << jointTree.computeJointLoglk() << std::endl;
      std::cerr << " rank " << ParallelContext::getRank() << std::endl;
      std::cerr << "Move: " << *move << std::endl;
      exit(1);
    }
  }
}

//#define STOP

bool SearchUtils::findBestMove(JointTree &jointTree,
    std::vector<std::shared_ptr<Move> > &allMoves,
    double &bestLoglk,
    unsigned int &bestMoveIndex,
    bool blo,
    bool check)
{
  bestMoveIndex = static_cast<unsigned int>(-1);
  double initialLoglk = bestLoglk; //jointTree.computeJointLoglk();
  double initialReconciliationLoglk = jointTree.computeReconciliationLoglk();
  double initialLibpllLoglk = jointTree.computeLibpllLoglk();
  double averageReconciliationDiff = 0;
  assert(fabs(initialLoglk 
        - (initialReconciliationLoglk + initialLibpllLoglk)) < 0.000000001);
  auto begin = ParallelContext::getBegin(static_cast<unsigned int>(allMoves.size()));
  auto end = ParallelContext::getEnd(static_cast<unsigned int>(allMoves.size()));
  unsigned int bestRank = 0;
#ifdef STOP
  // ensure that all cores recieve the same number of tasks, 
  // to avoid deadlock when synchronizing
  while ((end - begin) * ParallelContext::getSize() < allMoves.size()) {
    if (begin > 0) {
      begin -= 1;
    } else {
      assert(end < allMoves.size());
      end += 1;
    }
  }
#endif
  for (auto i = begin; i < end; ++i) {
    auto move = allMoves[i];
    auto loglk = bestLoglk;
    SearchUtils::testMove(jointTree, move, 
        initialReconciliationLoglk,
        initialLibpllLoglk, 
        averageReconciliationDiff,
        loglk,
        blo,
        check);
    if (loglk > bestLoglk + 0.000000001) {
      bestLoglk = loglk;
      bestMoveIndex = i;
    }
#ifdef STOP
    if ((begin - i) % 1 == 0) {
      ParallelContext::getMax(bestLoglk, bestRank);
      ParallelContext::broadcastUInt(bestRank, bestMoveIndex);
      if (bestMoveIndex != static_cast<unsigned int>(-1)) {
        return true;
      }
    }
#endif
  }
  ParallelContext::getMax(bestLoglk, bestRank);
  ParallelContext::broadcastUInt(bestRank, bestMoveIndex);
  return bestMoveIndex != static_cast<unsigned int>(-1);
}
