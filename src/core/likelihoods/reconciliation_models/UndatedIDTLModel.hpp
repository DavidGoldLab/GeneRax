#pragma once

#include <likelihoods/reconciliation_models/AbstractReconciliationModel.hpp>
#include <likelihoods/LibpllEvaluation.hpp>
#include <IO/GeneSpeciesMapping.hpp>
#include <IO/Logger.hpp>
#include <algorithm>


/*
* Undated reconciliation model, accouting for duplication, loss, transfers
* and ILS with depth 1
*/
template <class REAL>
class UndatedIDTLModel: public AbstractReconciliationModel<REAL> {
public:
  UndatedIDTLModel(PLLRootedTree &speciesTree, 
      const GeneSpeciesMapping &geneSpeciesMappingp, 
      bool rootedGeneTree, 
      double minGeneBranchLength,
      bool pruneSpeciesTree):
    
    AbstractReconciliationModel<REAL>(speciesTree, geneSpeciesMappingp, rootedGeneTree, minGeneBranchLength, pruneSpeciesTree)
  {
  } 
  UndatedIDTLModel(const UndatedIDTLModel &) = delete;
  UndatedIDTLModel & operator = (const UndatedIDTLModel &) = delete;
  UndatedIDTLModel(UndatedIDTLModel &&) = delete;
  UndatedIDTLModel & operator = (UndatedIDTLModel &&) = delete;
  virtual ~UndatedIDTLModel();
  
  // overloaded from parent
  virtual void setRates(const RatesVector &rates);
  
  virtual void rollbackToLastState();
protected:
  // overloaded from parent
  virtual void setInitialGeneTree(PLLUnrootedTree &tree);
  // overloaded from parent
  virtual void updateCLV(pll_unode_t *geneNode);
  // overload from parent
  virtual void recomputeSpeciesProbabilities();
  // overloaded from parent
  virtual REAL getGeneRootLikelihood(pll_unode_t *root) const;
  // overload from parent
  virtual void computeGeneRootLikelihood(pll_unode_t *virtualRoot);
  virtual REAL getGeneRootLikelihood(pll_unode_t *root, pll_rnode_t *speciesRoot) {
    return _dtlclvs[root->node_index + this->_maxGeneId + 1]._uq[speciesRoot->node_index];
  }
  virtual REAL getLikelihoodFactor() const;
  virtual void beforeComputeLogLikelihood(); 
  virtual void afterComputeLogLikelihood(); 
  virtual void computeProbability(pll_unode_t *geneNode, pll_rnode_t *speciesNode, 
      REAL &proba,
      bool isVirtualRoot = false,
      Scenario *scenario = nullptr,
      Scenario::Event *event = nullptr,
      bool stochastic = false);
private:
  // model
  std::vector<double> _PD; // Duplication probability, per branch
  std::vector<double> _PL; // Loss probability, per branch
  std::vector<double> _PT; // Transfer probability, per branch
  std::vector<double> _PS; // Speciation probability, per branch
  std::vector<double> _PI; // ILS probability, per branch
  // SPECIES
  std::vector<REAL> _uE; // Probability for a gene to become extinct on each brance
  REAL _transferExtinctionSum;
  REAL _transferExtinctionSumBackup;

  
  /**
   *  All intermediate results needed to compute the reconciliation likelihood
   *  each gene node has one DTLCLV object
   *  Each DTLCLV gene  object is a function of the DTLCLVs of the direct children genes
   */
  struct DTLCLV {
    DTLCLV():
      _survivingTransferSums(REAL()),
      _survivingTransferSumsInvariant(REAL()),
      _survivingTransferSumsOneMore(REAL())
    {}

    DTLCLV(unsigned int speciesNumber):
      _uq(speciesNumber, REAL()),
      _survivingTransferSums(REAL()),
      _survivingTransferSumsInvariant(REAL()),
      _survivingTransferSumsOneMore(REAL())
    {}
    // probability of a gene node rooted at a species node
    std::vector<REAL> _uq;
    // sum of transfer probabilities. Can be computed only once
    // for all species, to reduce computation complexity
    REAL _survivingTransferSums;
    // subsum of transfer probbabilities that did not change
    // in case of partial likelihood recomputation
    REAL _survivingTransferSumsInvariant;
    // when computing the likelihood in slow mode, we update this value,
    // because we need it to compute _survivingTransferSumsInvariant
    // consistently in fast mode
    REAL _survivingTransferSumsOneMore;
  };

  // Current DTLCLV values
  std::vector<DTLCLV> _dtlclvs;
  // Previous DTLCVL values, to rollback to a consistent state
  // after a fast likelihood computation
  std::vector<DTLCLV> _dtlclvsBackup;
private:
  void updateTransferSums(REAL &transferExtinctionSum,
    const REAL &transferSumBackup,
    const std::vector<REAL> &probabilities);
  void resetTransferSums(const REAL &transferSum,
    REAL &transferSumBackup,
    const std::vector<REAL> &probabilities);
  void getBestTransfer(pll_unode_t *parentGeneNode, 
    pll_rnode_t *originSpeciesNode,
    bool isVirtualRoot,
    pll_unode_t *&transferedGene,
    pll_unode_t *&stayingGene,
    pll_rnode_t *&recievingSpecies,
    REAL &proba, 
    bool stochastic = false);
  void getBestTransferLoss(Scenario &scenario,
      pll_unode_t *parentGeneNode, 
    pll_rnode_t *originSpeciesNode,
    pll_rnode_t *&recievingSpecies,
    REAL &proba,
    bool stochastic = false);
  unsigned int getIterationsNumber() const { return this->_fastMode ? 1 : 5;}    
  REAL getCorrectedTransferExtinctionSum(unsigned int speciesId) const {
    return _transferExtinctionSum * _PT[speciesId];
  }

  REAL getCorrectedTransferSum(unsigned int geneId, unsigned int speciesId) const
  {
    return _dtlclvs[geneId]._survivingTransferSums * _PT[speciesId];
  }
  std::vector<pll_rnode_s *> &getSpeciesNodesToUpdate() {
    return (this->_fastMode ? this->_speciesNodesToUpdate : this->_allSpeciesNodes);
  }
};




template <class REAL>
void UndatedIDTLModel<REAL>::setInitialGeneTree(PLLUnrootedTree &tree)
{
  AbstractReconciliationModel<REAL>::setInitialGeneTree(tree);
  assert(this->_allSpeciesNodesCount);
  assert(this->_maxGeneId);
  std::vector<REAL> zeros(this->_allSpeciesNodesCount);
  DTLCLV nullCLV(this->_allSpeciesNodesCount);
  _dtlclvs = std::vector<DTLCLV>(2 * (this->_maxGeneId + 1), nullCLV);
  _dtlclvsBackup = std::vector<DTLCLV>(2 * (this->_maxGeneId + 1), nullCLV);
}

  template <class REAL>
void UndatedIDTLModel<REAL>::resetTransferSums(const REAL &transferSum,
    REAL &transferSumInvariant,
    const std::vector<REAL> &probabilities)
{
  if (this->_fastMode) {
    REAL diff = REAL();
    for (auto speciesNode: getSpeciesNodesToUpdate()) {
      diff += probabilities[speciesNode->node_index];
    }
    diff /= this->_allSpeciesNodes.size();
    transferSumInvariant = transferSum - diff;
  }
}

template <class REAL>
void UndatedIDTLModel<REAL>::updateTransferSums(REAL &transferSum,
    const REAL &transferSumInvariant,
    const std::vector<REAL> &probabilities)
{
  transferSum = REAL();
  for (auto speciesNode:  getSpeciesNodesToUpdate()) {
    auto e = speciesNode->node_index;
    transferSum += probabilities[e];
  }
  transferSum /= this->_allSpeciesNodes.size();
  if (this->_fastMode) {
    transferSum += transferSumInvariant;
  }
}


template <class REAL>
void UndatedIDTLModel<REAL>::setRates(const RatesVector &rates)
{
  assert(rates.size() == 4);
  auto &dupRates = rates[0];
  auto &lossRates = rates[1];
  auto &transferRates = rates[2];
  auto &ilsRates = rates[3];
  this->_geneRoot = 0;
  assert(this->_allSpeciesNodesCount == dupRates.size());
  assert(this->_allSpeciesNodesCount == lossRates.size());
  assert(this->_allSpeciesNodesCount == transferRates.size());
  assert(this->_allSpeciesNodesCount == ilsRates.size());
  _PD = dupRates;
  _PL = lossRates;
  _PT = transferRates;
  _PI = ilsRates;
  _PS.resize(this->_allSpeciesNodesCount);
  for (auto speciesNode: this->_allSpeciesNodes) {
    auto e = speciesNode->node_index;
    _PS[e] = 1.0;
    if (!speciesNode->left || !speciesNode->parent) {
      _PI[e] = 0.0;
    }
    auto sum = _PD[e] + _PL[e] + _PT[e] + _PS[e];
    if (speciesNode->left) {
      auto f = speciesNode->left->node_index;
      auto g = speciesNode->right->node_index;
      sum += _PI[f] + _PI[g];
      _PI[f] /= sum;
      _PI[g] /= sum;
    }
    _PD[e] /= sum;
    _PL[e] /= sum;
    _PT[e] /= sum;
    _PS[e] /= sum;
  } 

  recomputeSpeciesProbabilities();
  this->invalidateAllCLVs();
  this->invalidateAllSpeciesCLVs();
}

template <class REAL>
UndatedIDTLModel<REAL>::~UndatedIDTLModel() { }

template <class REAL>
void UndatedIDTLModel<REAL>::recomputeSpeciesProbabilities()
{
  _uE.resize(this->_allSpeciesNodesCount);
  REAL unused = REAL(1.0);
  resetTransferSums(_transferExtinctionSum, unused, _uE);
  for (unsigned int it = 0; it < getIterationsNumber(); ++it) {
    for (auto speciesNode: getSpeciesNodesToUpdate()) {
      auto e = speciesNode->node_index;
      REAL proba(_PL[e]);
      REAL temp = _uE[e] * _uE[e] * _PD[e];
      scale(temp);
      proba += temp;
      temp = getCorrectedTransferExtinctionSum(e) * _uE[e];
      scale(temp);
      proba += temp;
      if (this->getSpeciesLeft(speciesNode)) {
        temp = _uE[this->getSpeciesLeft(speciesNode)->node_index]  * _uE[this->getSpeciesRight(speciesNode)->node_index] * _PS[e];
        scale(temp);
        proba += temp;

        auto left = this->getSpeciesLeft(speciesNode);
        auto right = this->getSpeciesRight(speciesNode);
        // todobenoit is it _PI[e], or should we use children species rates? where does the ILS really happen??
        if (this->getSpeciesLeft(left)) {
          auto leftleft = this->getSpeciesLeft(left);
          auto leftright = this->getSpeciesRight(left);
          proba += _uE[leftleft->node_index] * _uE[leftright->node_index] * _uE[right->node_index] * _PI[left->node_index];
        }
        if (this->getSpeciesLeft(right)) {
          auto rightleft = this->getSpeciesLeft(right);
          auto rightright = this->getSpeciesRight(right);
          proba += _uE[rightleft->node_index] * _uE[rightright->node_index] * _uE[left->node_index] * _PI[right->node_index];        
        }
      }
      //PRINT_ERROR_PROBA(proba)
      _uE[speciesNode->node_index] = proba;
    }
    updateTransferSums(_transferExtinctionSum, unused, _uE);
  }
}


template <class REAL>
void UndatedIDTLModel<REAL>::updateCLV(pll_unode_t *geneNode)
{
  auto gid = geneNode->node_index;
  resetTransferSums(this->_fastMode ? _dtlclvs[gid]._survivingTransferSumsOneMore : _dtlclvs[gid]._survivingTransferSums, _dtlclvs[gid]._survivingTransferSumsInvariant, _dtlclvs[gid]._uq);
  
  if (!this->_fastMode) {
    for (auto speciesNode: getSpeciesNodesToUpdate()) {
      _dtlclvs[gid]._uq[speciesNode->node_index] = REAL();
    }
  }
  for (unsigned int it = 0; it < getIterationsNumber(); ++it) {
    updateTransferSums(_dtlclvs[gid]._survivingTransferSums, _dtlclvs[gid]._survivingTransferSumsInvariant, _dtlclvs[gid]._uq);
    for (auto speciesNode: getSpeciesNodesToUpdate()) { 
      computeProbability(geneNode, 
          speciesNode, 
          _dtlclvs[gid]._uq[speciesNode->node_index]);
    }
  }
  if (this->_likelihoodMode == PartialLikelihoodMode::PartialSpecies && !this->_fastMode) {
    updateTransferSums(_dtlclvs[gid]._survivingTransferSumsOneMore, _dtlclvs[gid]._survivingTransferSumsInvariant, _dtlclvs[gid]._uq);
  }
}


template <class REAL>
void UndatedIDTLModel<REAL>::computeProbability(pll_unode_t *geneNode, pll_rnode_t *speciesNode, 
      REAL &proba,
      bool isVirtualRoot,
      Scenario *scenario,
      Scenario::Event *event,
      bool stochastic)
{
  
  auto gid = geneNode->node_index;
  auto e = speciesNode->node_index;
  bool isGeneLeaf = !geneNode->next;
  bool isSpeciesLeaf = !this->getSpeciesLeft(speciesNode);
  

  if (event) {
    event->geneNode = gid; 
    event->speciesNode = e;
    event->type = ReconciliationEventType::EVENT_None; 
  }

  if (isSpeciesLeaf and isGeneLeaf and e == this->_geneToSpecies[gid]) {
    proba = REAL(_PS[e]);
    return;
  }
  typedef std::array<REAL, 9>  ValuesArray;
  ValuesArray values;
  values[0] = values[1] = values[2] = values[3] = REAL();
  values[4] = values[5] = values[6] = values[7] = REAL();
  values[8] = REAL();
  proba = REAL();
  
  pll_unode_t *leftGeneNode = 0;     
  pll_unode_t *rightGeneNode = 0;     
  if (!isGeneLeaf) {
    leftGeneNode = this->getLeft(geneNode, isVirtualRoot);
    rightGeneNode = this->getRight(geneNode, isVirtualRoot);
  }
  unsigned int f = 0;
  unsigned int g = 0;
  if (!isSpeciesLeaf) {
    f = this->getSpeciesLeft(speciesNode)->node_index;
    g = this->getSpeciesRight(speciesNode)->node_index;
  }
  if (not isGeneLeaf) {
    // S event
    auto u_left = leftGeneNode->node_index;
    auto u_right = rightGeneNode->node_index;
    if (not isSpeciesLeaf) {
      //  speciation event
      values[0] = _dtlclvs[u_left]._uq[f];
      values[1] = _dtlclvs[u_left]._uq[g];
      values[0] *= _dtlclvs[u_right]._uq[g];
      values[1] *= _dtlclvs[u_right]._uq[f];
      values[0] *= _PS[e]; 
      values[1] *= _PS[e]; 
      scale(values[0]);
      scale(values[1]);
      proba += values[0];
      proba += values[1];
      // ILS event
      pll_unode_t *sonGeneNodes[2] = {leftGeneNode, rightGeneNode};
      pll_rnode_s *sonSpeciesNodes[2] = {
        this->getSpeciesLeft(speciesNode),
        this->getSpeciesRight(speciesNode)
      };
      pll_unode_t *grandSonGeneNodes[2][2] = {{0, 0}, {0, 0}};
      pll_rnode_t *grandSonSpeciesNodes[2][2] = {{0, 0}, {0, 0}};
      for (unsigned int i = 0; i < 2; ++i) {
        for (unsigned int j = 0; j < 2; ++j) {
          if (sonGeneNodes[i]->next) {
            grandSonGeneNodes[i][j] = this->getGeneSon(sonGeneNodes[i], j); 
          }
          grandSonSpeciesNodes[i][j] = this->getSpeciesSon(sonSpeciesNodes[i], j); 
        }
      }
      for (bool ilsSpecies: {false, true}) {
        for (bool ilsGene: {false, true}) {
          if (!(grandSonSpeciesNodes[ilsSpecies][0] 
              && grandSonGeneNodes[!ilsGene][0])) {
            continue;
          }
          
          for (bool lrgene: {false, true}) {
            for (bool lrspecies: {false, true}) {
              unsigned int g1 = sonGeneNodes[ilsGene]->node_index;
              unsigned int s1 = grandSonSpeciesNodes[ilsSpecies][lrspecies]->node_index;
              unsigned int g2 = grandSonGeneNodes[!ilsGene][lrgene]->node_index;
              unsigned int s2 = grandSonSpeciesNodes[ilsSpecies][!lrspecies]->node_index;
              unsigned int g3 = grandSonGeneNodes[!ilsGene][!lrgene]->node_index;
              unsigned int s3 = sonSpeciesNodes[!ilsSpecies]->node_index;
              REAL t = _dtlclvs[g1]._uq[s1];
              t *= _dtlclvs[g2]._uq[s2];
              t *= _dtlclvs[g3]._uq[s3];
              t *= _PI[sonSpeciesNodes[ilsSpecies]->node_index];
              scale(t);
              values[8] += t;
            }
          }
        }
      }
      proba += values[8];
    }
    // D event
    values[2] = _dtlclvs[u_left]._uq[e];
    values[2] *= _dtlclvs[u_right]._uq[e];
    values[2] *= _PD[e];
    scale(values[2]);
    proba += values[2];
    
    // T event
    values[5] = getCorrectedTransferSum(u_left, e);
    values[5] *= _dtlclvs[u_right]._uq[e];
    scale(values[5]);
    values[6] = getCorrectedTransferSum(u_right, e);
    values[6] *= _dtlclvs[u_left]._uq[e];
    scale(values[6]);
    proba += values[5];
    proba += values[6];
  }
  if (not isSpeciesLeaf) {
    // SL event
    values[3] = _dtlclvs[gid]._uq[f];
    values[3] *= (_uE[g] * _PS[e]);
    scale(values[3]);
    values[4] = _dtlclvs[gid]._uq[g];
    values[4]*= _uE[f] * _PS[e];
    scale(values[4]);
    proba += values[3];
    proba += values[4];
  }
  // TL event
  values[7] = getCorrectedTransferSum(gid, e);
  values[7] *= _uE[e];
  scale(values[7]);
  proba += values[7];
  
  if (event) {
    assert(scenario);
    pll_unode_t *transferedGene = 0;
    pll_unode_t *stayingGene = 0;
    pll_rnode_t *recievingSpecies = 0;
    pll_rnode_t *tlRecievingSpecies = 0;
    values[5] = values[6] = values[7] = REAL(); // invalidate these ones
    if (!isGeneLeaf) {
      getBestTransfer(geneNode, speciesNode, isVirtualRoot, 
          transferedGene, stayingGene, recievingSpecies, values[5], stochastic);
    }
    getBestTransferLoss(*scenario, geneNode, speciesNode, tlRecievingSpecies, values[7], stochastic);
    int maxValueIndex = 0;
    if (!stochastic) {
      maxValueIndex =static_cast<unsigned int>(std::distance(values.begin(),
          std::max_element(values.begin(), values.end())
          ));
    } else {
      maxValueIndex = sampleIndex<ValuesArray, REAL>(values);
    }
    if (-1 == maxValueIndex || values[maxValueIndex] == REAL()) {
      event->type = ReconciliationEventType::EVENT_Invalid;
      return;
    }
    switch(maxValueIndex) {
    case 0:
      event->type = ReconciliationEventType::EVENT_S;
      event->cross = false;
      break;
    case 1:
      event->type = ReconciliationEventType::EVENT_S;
      event->cross = true;
      break;
    case 2:
      event->type = ReconciliationEventType::EVENT_D;
      break;
    case 3:
      event->type = ReconciliationEventType::EVENT_SL;
      event->destSpeciesNode = f;
      event->pllDestSpeciesNode = this->getSpeciesLeft(speciesNode);
      break;
    case 4:
      event->type = ReconciliationEventType::EVENT_SL;
      event->destSpeciesNode = g;
      event->pllDestSpeciesNode = this->getSpeciesRight(speciesNode);
      break;
    case 5:
      event->type = ReconciliationEventType::EVENT_T;
      event->transferedGeneNode = transferedGene->node_index;
      event->destSpeciesNode = recievingSpecies->node_index;
      event->pllTransferedGeneNode = transferedGene;
      event->pllDestSpeciesNode = recievingSpecies;
      break;
    case 6:
      assert(false);
      break;
    case 7:
      event->type = ReconciliationEventType::EVENT_TL;
      event->transferedGeneNode = gid;
      event->destSpeciesNode = tlRecievingSpecies->node_index;
      event->pllTransferedGeneNode = geneNode;
      event->pllDestSpeciesNode = tlRecievingSpecies;
      break;
    default:
      assert(false);
    };
  }
}


template <class REAL>
void UndatedIDTLModel<REAL>::computeGeneRootLikelihood(pll_unode_t *virtualRoot)
{
  auto u = virtualRoot->node_index;
  resetTransferSums(this->_fastMode ? _dtlclvs[u]._survivingTransferSumsOneMore : _dtlclvs[u]._survivingTransferSums, _dtlclvs[u]._survivingTransferSumsInvariant, _dtlclvs[u]._uq);
  if (!this->_fastMode) {
    for (auto speciesNode: getSpeciesNodesToUpdate()) {
      auto e = speciesNode->node_index;
      _dtlclvs[u]._uq[e] = REAL();
    }
  }
  for (unsigned int it = 0; it < getIterationsNumber(); ++it) {
    updateTransferSums(_dtlclvs[u]._survivingTransferSums, _dtlclvs[u]._survivingTransferSumsInvariant, _dtlclvs[u]._uq);
    for (auto speciesNode: getSpeciesNodesToUpdate()) {
      unsigned int e = speciesNode->node_index;
      computeProbability(virtualRoot, speciesNode, _dtlclvs[u]._uq[e], true);
    }
  }
  if (!this->_fastMode) {
    updateTransferSums(_dtlclvs[u]._survivingTransferSumsOneMore, _dtlclvs[u]._survivingTransferSumsInvariant, _dtlclvs[u]._uq);
  }
}


template <class REAL>
REAL UndatedIDTLModel<REAL>::getGeneRootLikelihood(pll_unode_t *root) const
{
  REAL sum = REAL();
  auto u = root->node_index + this->_maxGeneId + 1;
  for (auto speciesNode: this->_allSpeciesNodes) {
    auto e = speciesNode->node_index;
    sum += _dtlclvs[u]._uq[e];
  }
  PRINT_ERROR_PROBA(sum);
  assert(IS_PROBA(sum));
  return sum;
}

template <class REAL>
REAL UndatedIDTLModel<REAL>::getLikelihoodFactor() const
{
  REAL factor(0.0);
  for (auto speciesNode: this->_allSpeciesNodes) {
    auto e = speciesNode->node_index;
    factor += (REAL(1.0) - _uE[e]);
  }
  return factor;
      
}

template <class REAL>
void UndatedIDTLModel<REAL>::beforeComputeLogLikelihood()
{
  AbstractReconciliationModel<REAL>::beforeComputeLogLikelihood();
  if (this->_likelihoodMode == PartialLikelihoodMode::PartialSpecies) {
    if (this->_fastMode) {
      _transferExtinctionSumBackup = _transferExtinctionSum;
      for (unsigned int gid = 0; gid < _dtlclvs.size(); ++gid) {
        _dtlclvsBackup[gid]._survivingTransferSums = _dtlclvs[gid]._survivingTransferSums;
        for (auto speciesNode: getSpeciesNodesToUpdate()) {
          auto e = speciesNode->node_index;
          _dtlclvsBackup[gid]._uq[e] = _dtlclvs[gid]._uq[e];
        }
      }
    } else { 
      std::swap(_dtlclvs, _dtlclvsBackup);
    }
  }
}

template <class REAL>
void UndatedIDTLModel<REAL>::afterComputeLogLikelihood()
{
  AbstractReconciliationModel<REAL>::afterComputeLogLikelihood();
  if (this->_likelihoodMode == PartialLikelihoodMode::PartialSpecies) {
    if (this->_fastMode) {
      _transferExtinctionSum = _transferExtinctionSumBackup;
      for (unsigned int gid = 0; gid < _dtlclvs.size(); ++gid) {
        _dtlclvs[gid]._survivingTransferSums = _dtlclvsBackup[gid]._survivingTransferSums;
        for (auto speciesNode: getSpeciesNodesToUpdate()) {
          auto e = speciesNode->node_index;
          _dtlclvs[gid]._uq[e] = _dtlclvsBackup[gid]._uq[e];
        }
      }
    }
  }
}

template <class REAL>
void UndatedIDTLModel<REAL>::rollbackToLastState()
{
  std::swap(_dtlclvs, _dtlclvsBackup);
}

template <class REAL>
void UndatedIDTLModel<REAL>::getBestTransfer(pll_unode_t *parentGeneNode, 
  pll_rnode_t *originSpeciesNode,
  bool isVirtualRoot,
  pll_unode_t *&transferedGene,
  pll_unode_t *&stayingGene,
  pll_rnode_t *&recievingSpecies,
  REAL &proba, 
  bool stochastic)
{
  unsigned int speciesNumber = this->_allSpeciesNodes.size();
  proba = REAL();
  auto e = originSpeciesNode->node_index;
  auto u_left = this->getLeft(parentGeneNode, isVirtualRoot);
  auto u_right = this->getRight(parentGeneNode, isVirtualRoot);
  std::unordered_set<unsigned int> parents;
  parents.insert(originSpeciesNode->node_index);
  for (auto parent = originSpeciesNode; this->getSpeciesParent(parent) != 0; parent = this->getSpeciesParent(parent)) {
    parents.insert(this->getSpeciesParent(parent)->node_index);
  }
  std::vector<REAL> transferProbas(speciesNumber * 2, REAL());
  double factor = _PT[e] / static_cast<double>(speciesNumber);
  for (auto species: this->_allSpeciesNodes) {
    auto h = species->node_index;
    if (parents.count(h)) {
      continue;
    }
    transferProbas[h] = (_dtlclvs[u_left->node_index]._uq[h] 
        * _dtlclvs[u_right->node_index]._uq[e]) * factor;
    transferProbas[h + speciesNumber] = (_dtlclvs[u_right->node_index]._uq[h] 
        * _dtlclvs[u_left->node_index]._uq[e]) * factor;
  }
  if (stochastic) {
    // stochastic sample: proba will be set to the sum of probabilities
    proba = REAL();
    for (auto &value: transferProbas) {
      proba += value;
    }
    auto bestIndex = sampleIndex<std::vector<REAL>, REAL>(transferProbas);
    if (bestIndex == -1) {
      proba = REAL();
      return;
    }
    bool left = static_cast<unsigned int>(bestIndex) < speciesNumber;
    transferedGene = left ? u_left : u_right;
    stayingGene = !left ? u_left : u_right;
    // I am not sure I can find the species from
    // its index in the species array, so I keep it safe here
    for (auto species: this->_allSpeciesNodes) {
      if (species->node_index == bestIndex % speciesNumber) {
        recievingSpecies = species;
      }
    }
  } else {
    // find the max
    for (auto species: this->_allSpeciesNodes) {
      auto h = species->node_index;
      if (proba < transferProbas[h]) {
        proba = transferProbas[h];
        transferedGene = u_left;
        stayingGene = u_right;
        recievingSpecies = species;
      }
      if (proba < transferProbas[h + speciesNumber]) {
        proba = transferProbas[h + speciesNumber];
        transferedGene = u_right;
        stayingGene = u_left;
        recievingSpecies = species;
      }
    }
  }
}

template <class REAL>
void UndatedIDTLModel<REAL>::getBestTransferLoss(Scenario &scenario,
   pll_unode_t *parentGeneNode, 
  pll_rnode_t *originSpeciesNode,
  pll_rnode_t *&recievingSpecies,
  REAL &proba, 
  bool stochastic)
{
  proba = REAL();
  auto e = originSpeciesNode->node_index;
  auto u = parentGeneNode->node_index;
  std::unordered_set<unsigned int> parents;
  parents.insert(originSpeciesNode->node_index);
  unsigned int speciesNumber = this->_allSpeciesNodes.size();
  std::vector<REAL> transferProbas(speciesNumber, REAL());
  for (auto parent = originSpeciesNode; this->getSpeciesParent(parent) != 0; parent = this->getSpeciesParent(parent)) {
    parents.insert(this->getSpeciesParent(parent)->node_index);
  }
  REAL factor = _uE[e] * (_PT[e] / static_cast<double>(this->_allSpeciesNodes.size()));
  for (auto species: this->_allSpeciesNodes) {
    auto h = species->node_index;
    if (parents.count(h)) {
      continue;
    }
    transferProbas[h] = _dtlclvs[u]._uq[h] * factor;
  }
  if (!stochastic) {
    for (auto species: this->_allSpeciesNodes) {
      auto h = species->node_index;
      if (proba < transferProbas[h]) {
        if (!scenario.isBlacklisted(u, h)) {
          scenario.blackList(u, h);
          proba = transferProbas[h];
          recievingSpecies = species;
        }
      }
    } 
  } else {
    proba = REAL();
    for (auto p: transferProbas) {
      proba += p;
    }
    unsigned int h = 0;
    do {

      int bestIndex = sampleIndex<std::vector<REAL>, REAL>(transferProbas);
      if (bestIndex == -1) {
        proba = REAL();
        return;
      }
      for (auto species: this->_allSpeciesNodes) {
        h = species->node_index;
        if (static_cast<unsigned int>(bestIndex) == h) {
          recievingSpecies = species;
          transferProbas[h] = REAL(); // in case it's blacklisted, avoid infinite loop
          break;
        }
      }
    } while (scenario.isBlacklisted(u, h));
    scenario.blackList(u, h);
  }
}
