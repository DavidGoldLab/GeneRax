#include "Cherry.hpp"

#include <vector>
#include <memory>
#include "MiniNJ.hpp"
#include <IO/Logger.hpp>
#include <IO/GeneSpeciesMapping.hpp>
#include <trees/PLLUnrootedTree.hpp>
#include <unordered_map>

typedef std::vector<double> VectorDouble;
typedef std::vector<VectorDouble> MatrixDouble;
typedef std::unordered_map<std::string, int> StringToInt;
typedef std::unordered_set<int> GeneIdsSet;
typedef std::unordered_map<int, GeneIdsSet> SpeciesIdToGeneIds;


static const bool CHERRY_DBG = false;

struct CherryNode {
  bool isLeaf; 
  int sons[3];
  int geneId;
  int speciesId; // only for leaves
  bool isValid;
};

/**
 *  Unrooted gene tree for Cherry algorithm
 */
class CherryTree {
public:
  CherryTree(const std::string &treeString, 
      const GeneSpeciesMapping &mapping,
      const StringToInt &speciesStrToId);
  
  std::string toNewick();

  void mergeNodesWithSameSpeciesId();
  void mergeNodesWithSpeciesId(unsigned int speciesId);
  void relabelNodesWithSpeciesId(unsigned int speciesId, 
    unsigned int newSpeciesId);

  void updateNeigborMatrix(MatrixDouble &neighborMatrix,
      MatrixDouble &denominatorMatrix);
  int coveredSpeciesNumber();
  std::string toString();
  void printInternalState();
  unsigned int getLeavesNumber() {
    return _leavesNumber;
  }
private:
  int getAnyValidId();
  int getNeighborLeaf(int nodeId);
  std::string recursiveToString(int nodeId, int sonToSkipId);
  std::vector<CherryNode> _nodes;
  SpeciesIdToGeneIds _speciesIdToGeneIds;
  unsigned int _leavesNumber;
  static int hackCounter;
  int _hackIndex;
};

int CherryTree::hackCounter = 0;
  
void CherryTree::printInternalState()
{
  for (auto &node: _nodes) {
    if (node.isValid) {
      Logger::info << "gid=" << node.geneId;
      if (node.isLeaf) {
        Logger::info << " spid=" << node.speciesId << "parent=" << node.sons[0] << std::endl;
      } else {
        Logger::info << " " << node.sons[0] << " "  << node.sons[1] << " " << node.sons[2] << std::endl;
      }
    }
  }
  Logger::info << "Covered species " << coveredSpeciesNumber() << std::endl;
}

static void printMatrix(const MatrixDouble m) 
{
  for (auto &v: m) {
    for (auto e: v) {
      Logger::info << e << " ";
    }
    Logger::info << std::endl;
  }

}

static void divideMatrix(MatrixDouble &m, const MatrixDouble &denom)
{
  assert(m.size() == denom.size());
  assert(m[0].size() == denom[0].size());
  for (unsigned int i = 0; i < m.size(); ++i) {
    for (unsigned int j = 0; j < m.size(); ++j) {
      if (denom[i][j] != 0.0) {
        m[i][j] /= denom[i][j];
      }
    }
  }
}

static std::pair<int, int> getMaxInMatrix(MatrixDouble &m)
{
  assert(m.size());
  std::pair<int, int> minPair = {0, 0};
  for (unsigned int i = 0; i < m.size(); ++i) {
    for (unsigned int j = 0; j < m.size(); ++j) {
      if (m[minPair.first][minPair.second] < m[i][j]) {
        minPair = {i, j};    
      }
    }
  }
  return minPair;
}
  
void CherryTree::relabelNodesWithSpeciesId(unsigned int speciesId, 
    unsigned int newSpeciesId)
{
  if (_speciesIdToGeneIds.find(speciesId) == _speciesIdToGeneIds.end()) {
    return;
  }
  if (_speciesIdToGeneIds.find(newSpeciesId) == _speciesIdToGeneIds.end()) {
    _speciesIdToGeneIds.insert({newSpeciesId, GeneIdsSet()});
  }
  auto &newGeneIdSet = _speciesIdToGeneIds[newSpeciesId];
  for (auto geneId: _speciesIdToGeneIds[speciesId]) {
    auto &node = _nodes[geneId];
    node.speciesId = newSpeciesId;
    newGeneIdSet.insert(geneId);
  }
  _speciesIdToGeneIds.erase(speciesId);
}

void CherryTree::updateNeigborMatrix(MatrixDouble &neighborMatrixToUpdate,
      MatrixDouble &denominatorMatrixToUpdate)
{
  MatrixDouble *neighborMatrix = &neighborMatrixToUpdate;
  MatrixDouble *denominatorMatrix = &denominatorMatrixToUpdate;
  
  
  /*
  // settings
  const bool perFamilyWeight = false; 
  
  // init from settings
  auto speciesNumber = neighborMatrixToUpdate.size();
  bool intermediateMatrices = perFamilyWeight;
  bool deleteMatrices = false;
  if (intermediateMatrices) {
    VectorDouble zeros(speciesNumber);
    neighborMatrix = new MatrixDouble(speciesNumber, zeros);
    denominatorMatrix = new MatrixDouble(speciesNumber, zeros);
    deleteMatrices = true;
  }
  */

  for (auto &p: _speciesIdToGeneIds) {
    auto speciesId = p.first;
    // First fill neighborMatrix
    const auto &geneIdSet = p.second;
    for (auto geneId: geneIdSet) {
      auto neighborGeneId = getNeighborLeaf(geneId);
      if (neighborGeneId == -1) {
        // no leaf neighbor
        continue;
      }
      auto spid1 = _nodes[geneId].speciesId;
      auto spid2 = _nodes[neighborGeneId].speciesId;
      assert(spid1 == speciesId);
      (*neighborMatrix)[spid1][spid2] += 1.0;
    }
    // Then fill denominatorMatrix with
    // the maximum possible number of neighbors
    // between two species
    for (auto &p2: _speciesIdToGeneIds) {
      auto spid2 = p2.first; 
      const auto &geneIdSet2 = p2.second;
      (*denominatorMatrix)[speciesId][spid2] += 
        //geneIdSet.size();
        //geneIdSet2.size();
        //geneIdSet.size() +  geneIdSet2.size();
        std::min(geneIdSet.size(),  geneIdSet2.size());
    }
  }
 
  /*
  // update according to settings
  if (perFamilyWeight) {
    for (unsigned int i = 0; i < speciesNumber; ++i) {
      for (unsigned int j = 0; j < speciesNumber; ++j) {
        if ((*denominatorMatrix)[i][j] != 0.0) {
          neighborMatrixToUpdate[i][j] += (*neighborMatrix)[i][j] / (*denominatorMatrix)[i][j];
          neighborMatrixToUpdate[i][j] += 1.0;
        }
      }
    }
  }

  if (deleteMatrices) {
    delete denominatorMatrix;
    delete neighborMatrix;
  }  
  */
}
  
int CherryTree::coveredSpeciesNumber()
{
  return _speciesIdToGeneIds.size();
}
  
void CherryTree::mergeNodesWithSameSpeciesId()
{
  for (auto &p: _speciesIdToGeneIds) {
    mergeNodesWithSpeciesId(p.first);
  }
}


int CherryTree::getAnyValidId()
{
  for (auto &p: _speciesIdToGeneIds) {
    for (auto id: p.second) {
      return id;
    }
  }
  assert(false);
  return -1;
}
 
int CherryTree::getNeighborLeaf(int nodeId)
{
  auto &node = _nodes[nodeId];
  assert(node.isLeaf);
  assert(node.isValid);
  auto &parentNode = _nodes[node.sons[0]];
  if(parentNode.isLeaf) {
    Logger::info << "Error in " << _hackIndex << std::endl;
  }
  assert(!parentNode.isLeaf);
  for (int i = 0; i < 3; ++i) {
    auto &candidateNode = _nodes[parentNode.sons[i]];
    if (candidateNode.isLeaf && candidateNode.geneId != node.geneId) {
      return candidateNode.geneId;
    }
  }
  return -1;
}


void CherryTree::mergeNodesWithSpeciesId(unsigned int speciesId)
{
  if (_speciesIdToGeneIds.find(speciesId) == _speciesIdToGeneIds.end()) {
    return;
  }
  auto &geneSet = _speciesIdToGeneIds[speciesId];
  GeneIdsSet geneSetCopy = geneSet;
  for (auto geneId: geneSetCopy) { 
    while (true) {
      if (getLeavesNumber() < 4) {
        // nothing to merge!
        return;
      }
      if (geneSet.find(geneId) == geneSet.end()) {
        // we already erased this gene
        break;
      }
      auto geneIdNeighbor = getNeighborLeaf(geneId);
      if (geneIdNeighbor == -1) {
        // this leaf neighbor is not a leaf, continue
        break;
      }
      if (geneSet.find(geneIdNeighbor) != geneSet.end()) {
        auto &parent = _nodes[_nodes[geneId].sons[0]];
        parent.isLeaf = true;
        for (int i = 0; i < 3; ++i) {
          if (parent.sons[i] != geneIdNeighbor &&
              parent.sons[i] != geneId) {
            parent.sons[0] = parent.sons[i];
            break;
          }
        }
        // merge
        parent.speciesId = speciesId;
        geneSet.insert(parent.geneId);
        geneSet.erase(geneId);
        geneSet.erase(geneIdNeighbor);
        _nodes[geneId].isValid = false;
        _nodes[geneIdNeighbor].isValid = false;
        geneId = parent.geneId;
        _leavesNumber--;
      } else {
        break;
      }
    }
  }
}
  

std::string CherryTree::toString()
{
  return recursiveToString(getAnyValidId(), -1) + ";";
}

std::string CherryTree::recursiveToString(int nodeId, int sonToSkipId)
{
  auto &node = _nodes[nodeId];
  assert(node.isValid);
  // edge case: do not start the recursion from a leaf
  if (sonToSkipId == -1 && node.isLeaf) {
    return recursiveToString(node.sons[0], -1);
  }
  // edge case: leaf
  if (node.isLeaf) {
    return std::to_string(node.speciesId);
  }
  std::string res = "(";
  bool firstNodeWritten = false;
  for (int i = 0; i < 3; ++i) {
    if (sonToSkipId != node.sons[i]) {
      res += recursiveToString(node.sons[i], node.geneId);
      if ((!firstNodeWritten || sonToSkipId == -1) && i != 2) {
        res += ",";
        firstNodeWritten = true;
      }
    }
  }
  res += ")";
  return res;
}

static std::vector<int> computePLLIdToId(PLLUnrootedTree &pllTree)
{
  int currentId = 0;
  int maxPllNodeId = pllTree.getLeavesNumber()
    + pllTree.getInnerNodesNumber() * 3;
  std::vector<int> pllIdToId(maxPllNodeId, -1);
  for (auto pllNode: pllTree.getNodes()) {
    if (pllIdToId[pllNode->node_index] == -1) {
      pllIdToId[pllNode->node_index] = currentId;
      if (pllNode->next) {
        pllIdToId[pllNode->next->node_index] = currentId;
        pllIdToId[pllNode->next->next->node_index] = currentId;
      }
      currentId++;
    }
  }
  return pllIdToId;
}

CherryTree::CherryTree(const std::string &treeString, 
      const GeneSpeciesMapping &mapping,
      const StringToInt &speciesStrToId):
  _leavesNumber(0),
  _hackIndex(hackCounter++)
{
  PLLUnrootedTree pllTree(treeString, false);
  _nodes.resize(pllTree.getLeavesNumber() * 2 - 2);
  auto pllIdToId = computePLLIdToId(pllTree);
  for (auto pllNode: pllTree.getNodes()) {
    auto geneId = pllIdToId[pllNode->node_index];
    auto &nfjNode = _nodes[geneId];
    nfjNode.sons[0] = pllIdToId[pllNode->back->node_index];
    nfjNode.geneId = geneId;
    nfjNode.isValid = true;
    if (pllNode->next) {
      // internal node
      nfjNode.isLeaf = false;
      nfjNode.sons[1] = pllIdToId[pllNode->next->back->node_index];
      nfjNode.sons[2] = pllIdToId[pllNode->next->next->back->node_index];
      nfjNode.speciesId = -1;
    } else {
      // leaf node
      nfjNode.isLeaf = true;
      auto species = mapping.getSpecies(pllNode->label);
      nfjNode.speciesId = speciesStrToId.at(species);
      if (_speciesIdToGeneIds.find(nfjNode.speciesId) == 
          _speciesIdToGeneIds.end()) {
        _speciesIdToGeneIds.insert({nfjNode.speciesId, GeneIdsSet()});
      }
      _speciesIdToGeneIds[nfjNode.speciesId].insert(nfjNode.geneId);
      _leavesNumber++;
    }
  }
}


static void filterGeneTrees(std::vector<std::shared_ptr<CherryTree> > &geneTrees)
{
  auto geneTreesCopy = geneTrees;
  geneTrees.clear();
  for (auto geneTree: geneTreesCopy) {
    if (geneTree->getLeavesNumber() >= 4 && geneTree->coveredSpeciesNumber() > 2 ) {
      geneTrees.push_back(geneTree);
    } else {
    }
  }
}

std::unique_ptr<PLLRootedTree> Cherry::geneTreeCherry(const Families &families)
{
  // Init gene trees and frequency matrix
  std::vector<std::shared_ptr<CherryTree> > geneTrees;
  StringToInt speciesStrToId;
  std::vector<std::string> speciesIdToStr;
  
  // fill the structure that map speciesStr <-> speciesId
  // and create gene trees mapped with the species IDs
  for (auto &family: families) {
    GeneSpeciesMapping mapping;
    mapping.fill(family.mappingFile, family.startingGeneTree);
    auto coveredSpecies = mapping.getCoveredSpecies();
    for (auto &species: mapping.getCoveredSpecies()) {
      if (speciesStrToId.find(species) == speciesStrToId.end()) {
        speciesStrToId.insert({species, speciesIdToStr.size()});
        speciesIdToStr.push_back(species);
      }
    }
    std::ifstream reader(family.startingGeneTree);
    std::string line;
    while (std::getline(reader, line)) {
      geneTrees.push_back(std::make_shared<CherryTree>( 
          line, mapping, speciesStrToId));
    }
  }
  Logger::info << "Loaded " << geneTrees.size() << " gene trees" << std::endl;
  unsigned int speciesNumber = speciesStrToId.size();
  std::unordered_set<int> remainingSpeciesIds;
  for (unsigned int i = 0; i < speciesNumber; ++i) {
    remainingSpeciesIds.insert(i);
  }
  filterGeneTrees(geneTrees);
  for (auto geneTree: geneTrees) {
    geneTree->mergeNodesWithSameSpeciesId();
  }
  // main loop of the algorithm
  for (unsigned int i = 0; i < speciesNumber - 2; ++i) {
    Logger::info << std::endl;
    Logger::info << "*******************************" << std::endl;
    Logger::info << "Remaining species: " << remainingSpeciesIds.size() << std::endl;
    if (CHERRY_DBG) {
      Logger::info << "Species mappings:" << std::endl;
      for (auto spid: remainingSpeciesIds) {
        Logger::info << "  " << spid << "\t" << speciesIdToStr[spid] << std::endl;
      }
    }
      // filter out gene trees that do not hold information
    filterGeneTrees(geneTrees);
    // merge identical tips in cherries
    // compute the frequency matrix
    if (CHERRY_DBG) {
      for (auto &tree: geneTrees) {
        Logger::info << "Tree " <<  tree->toString() << std::endl;
      }
    }
    VectorDouble zeros(speciesNumber);
    MatrixDouble neighborMatrix(speciesNumber, zeros);
    MatrixDouble denominatorMatrix(speciesNumber, zeros);
    for (auto geneTree: geneTrees) {
      geneTree->updateNeigborMatrix(neighborMatrix, denominatorMatrix);
    }
    if (CHERRY_DBG) {
      Logger::info << "Neighbors: " << std::endl;
      printMatrix(neighborMatrix);
      Logger::info << "Denominators: " << std::endl;
      printMatrix(denominatorMatrix);
    }
    divideMatrix(neighborMatrix, denominatorMatrix);
    if (CHERRY_DBG) {
      Logger::info << "Frequencies: " << std::endl;
      printMatrix(neighborMatrix);
    }
    // compute the two species to join, and join them
    auto bestPairSpecies = getMaxInMatrix(neighborMatrix);
    std::string speciesStr1 = speciesIdToStr[bestPairSpecies.first];
    std::string speciesStr2 = speciesIdToStr[bestPairSpecies.second];
    Logger::info << "Best pair " << bestPairSpecies.first
      << " " << bestPairSpecies.second << std::endl;
    Logger::info << "Best pair " << speciesStr1 << " " << speciesStr2 << std::endl;
    for (auto geneTree: geneTrees) {
      geneTree->relabelNodesWithSpeciesId(bestPairSpecies.second,
        bestPairSpecies.first);
      geneTree->mergeNodesWithSpeciesId(bestPairSpecies.first);
    }
    speciesIdToStr[bestPairSpecies.first] = std::string("(") + 
      speciesStr1 + "," + speciesStr2 + ")";
    remainingSpeciesIds.erase(bestPairSpecies.second);
  }
  std::vector<std::string> lastSpecies;
  for (auto speciesId: remainingSpeciesIds) {
    lastSpecies.push_back(speciesIdToStr[speciesId]);
  }
  assert(lastSpecies.size() == 2);
  std::string newick = "(" + lastSpecies[0] + "," + lastSpecies[1] + ");";
  return std::make_unique<PLLRootedTree>(newick, false); 
}



