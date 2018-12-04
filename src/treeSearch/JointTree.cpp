#include <treeSearch/JointTree.h>
#include <treeSearch/Moves.h>
#include <chrono>
#include <ParallelContext.hpp>
#include<limits>
#include <functional>


size_t leafHash(pll_unode_t *leaf) {
  hash<string> hash_fn;
  return hash_fn(string(leaf->label));
}

size_t getTreeHashRec(pll_unode_t *node, size_t i) {
  if (i == 0) 
    i = 1;
  if (!node->next) {
    return leafHash(node);
  }
  int hash1 = getTreeHashRec(node->next->back, i + 1);
  int hash2 = getTreeHashRec(node->next->next->back, i + 1);
  //Logger::info << "(" << hash1 << "," << hash2 << ") ";
  hash<int> hash_fn;
  int m = min(hash1, hash2);
  int M = max(hash1, hash2);
  return hash_fn(m * i + M);

}

pll_unode_t *findMinimumHashLeafRec(pll_unode_t * root, size_t &hash)
{
  if (!root->next) {
    hash = leafHash(root);
    return root;
  }
  auto n1 = root->next->back;
  auto n2 = root->next->next->back;
  size_t hash1, hash2;
  auto min1 = findMinimumHashLeafRec(n1, hash1);
  auto min2 = findMinimumHashLeafRec(n2, hash2);
  if (hash1 < hash2) {
    hash = hash1;
    return min1;
  } else {
    hash = hash2;
    return min2;
  }
}

pll_unode_t *findMinimumHashLeaf(pll_unode_t * root) 
{
  auto n1 = root;
  auto n2 = root->back;
  size_t hash1, hash2;
  auto min1 = findMinimumHashLeafRec(n1, hash1);
  auto min2 = findMinimumHashLeafRec(n2, hash2);
  if (hash1 < hash2) {
    return min1;
  } else {
    return min2;
  }
}
    
size_t JointTree::getUnrootedTreeHash()
{
  auto minHashLeaf = findMinimumHashLeaf(getTreeInfo()->root);
  auto res = getTreeHashRec(minHashLeaf, 0) + getTreeHashRec(minHashLeaf->back, 0);
  return res % 100000;
}

void printLibpllNode(pll_unode_s *node, Logger &os, bool isRoot)
{
  if (node->next) {
    os << "(";
    printLibpllNode(node->next->back, os, false);
    os << ",";
    printLibpllNode(node->next->next->back, os, false);
    os << ")";
  } else {
    os << node->label;
  }
  os << ":" << (isRoot ? node->length / 2.0 : node->length);
}

void printLibpllTreeRooted(pll_unode_t *root, Logger &os){
  os << "(";
  printLibpllNode(root, os, true);
  os << ",";
  printLibpllNode(root->back, os, true);
  os << ");" << endl;
}


JointTree::JointTree(const string &newick_string,
    const string &alignment_file,
    const string &speciestree_file,
    const string &geneSpeciesMap_file,
    Arguments::ReconciliationModel reconciliationModel,
    double dupRate,
    double lossRate):
  geneSpeciesMap_(geneSpeciesMap_file),
  dupRate_(dupRate),
  lossRate_(lossRate)
{
   info_.alignmentFilename = alignment_file;
  info_.model = "GTR";
  libpllEvaluation_ = LibpllEvaluation::buildFromString(newick_string, info_.alignmentFilename, info_.model);
  pllSpeciesTree_ = pll_rtree_parse_newick(speciestree_file.c_str());
  assert(pllSpeciesTree_);
  reconciliationEvaluation_ = make_shared<ReconciliationEvaluation>(pllSpeciesTree_,  
      geneSpeciesMap_, 
      reconciliationModel);
  setRates(dupRate, lossRate);

}

void JointTree::printLibpllTree() const {
  printLibpllTreeRooted(libpllEvaluation_->getTreeInfo()->root, Logger::info);
}



void JointTree::optimizeParameters() {
  if (!Arguments::noFelsensteinLikelihood) {
    libpllEvaluation_->optimizeAllParameters();
  }
  if (Arguments::costsEstimation) {
    optimizeDTRates();
  }
}

double JointTree::computeLibpllLoglk() {
  if (Arguments::noFelsensteinLikelihood) {
    return 0;
  }
  return libpllEvaluation_->computeLikelihood();
}

double JointTree::computeReconciliationLoglk () {
  return reconciliationEvaluation_->evaluate(libpllEvaluation_->getTreeInfo());
}

double JointTree::computeJointLoglk() {
  return computeLibpllLoglk() + computeReconciliationLoglk();
}

void JointTree::printLoglk(bool libpll, bool rec, bool joint, Logger &os) {
  if (joint)
    os << "joint: " << computeJointLoglk() << "  ";
  if (libpll)
    os << "libpll: " << computeLibpllLoglk() << "  ";
  if (rec)
    os << "reconciliation: " << computeReconciliationLoglk() << "  ";
  os << endl;
}


// todobenoit make it faster
pll_unode_t *JointTree::getNode(int index) {
  return getTreeInfo()->subnodes[index];
}


void JointTree::applyMove(shared_ptr<Move> move) {
  auto rollback = move->applyMove(*this);
  rollbacks_.push(rollback);
}


void JointTree::rollbackLastMove() {
  assert(!rollbacks_.empty());
  rollbacks_.top()->applyRollback();
  rollbacks_.pop();
}

void JointTree::save(const string &fileName, bool append) {
  ofstream os(fileName, (append ? ofstream::app : ofstream::out));
  char *newick = pll_utree_export_newick(getTreeInfo()->root, 0);
  os << newick;
}

shared_ptr<pllmod_treeinfo_t> JointTree::getTreeInfo() {
  return libpllEvaluation_->getTreeInfo();
}

void JointTree::optimizeDTRates() {
  double bestLL = numeric_limits<double>::lowest();
  double bestDup = 0.0;
  double bestLoss = 0.0;
  double min = 0.1;
  double max = 5.0;
  int steps = 25;
  int begin = ParallelContext::getBegin(steps * steps);
  int end = ParallelContext::getEnd(steps * steps);
  
  for (int s = begin; s < end; ++s) {
    int i = s / steps;
    int j = s % steps;
    /*
    if (i == j)
      continue;
      */
    double dup = min + (max - min) * double(i) / double(steps);
    double loss = min + (max - min) * double(j) / double(steps);
    setRates(dup, loss);
    double newLL = computeReconciliationLoglk();
    if (newLL > bestLL) { 
      bestDup = dup;
      bestLoss = loss;
      bestLL = newLL;
    }
  }
  int bestRank = 0;
  ParallelContext::getBestLL(bestLL, bestRank);
  ParallelContext::broadcoastDouble(bestRank, bestDup);
  ParallelContext::broadcoastDouble(bestRank, bestLoss);
  setRates(bestDup, bestLoss);
  Logger::info << " best rates: " << bestDup << " " << bestLoss << endl;
}

void JointTree::setRates(double dup, double loss) { 
  dupRate_ = dup; 
  lossRate_ = loss;
  reconciliationEvaluation_->setRates(dup, loss);
}

void printPGM(const string &output, vector<vector<double > > &array) {
  const int dimx = array.size(), dimy = array[0].size();
  FILE *fp = fopen(output.c_str(), "wb"); /* b - binary mode */
  (void) fprintf(fp, "P6\n%d %d\n255\n", dimx, dimy);
  for (int j = 0; j < dimy; ++j)
  {
    for (int i = 0; i < dimx; ++i)
    {
      static unsigned char color[3];
      color[0] = array[i][j] * 255.0;  /* red */
      color[1] = (1 - array[i][j]) * 255.0;  /* green */
      color[2] = (1 - array[i][j]) * 255.0;  /* blue */
      (void) fwrite(color, 1, 3, fp);
    }
  }
  (void) fclose(fp);
}
    
void JointTree::printRates(double min, double max, int resolution, const string &output)
{
  double maxLL = numeric_limits<double>::lowest();
  double minLL = numeric_limits<double>::max();
  vector<double> zeros(resolution, 0);
  vector<vector<double > > ll(resolution, zeros);
  for (int i = 0; i < resolution; ++i) {
    for (int j = 0; j < resolution; ++j) {
      double dup = min + (max - min) * double(i) / double(resolution);
      double loss = min + (max - min) * double(j) / double(resolution);
      setRates(dup, loss);
      ll[i][j] = computeReconciliationLoglk();
      maxLL = std::max(maxLL, ll[i][j]);
      minLL = std::min(minLL, ll[i][j]);
    }
  }

  for (int i = 0; i < resolution; ++i) {
    for (int j = 0; j < resolution; ++j) {
      ll[i][j] = (ll[i][j] - minLL) / (maxLL - minLL);
    }
  }
  printPGM(output, ll); 


}

