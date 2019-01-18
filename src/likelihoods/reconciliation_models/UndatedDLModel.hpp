#pragma once

#include <likelihoods/reconciliation_models/AbstractReconciliationModel.hpp>
#include <likelihoods/LibpllEvaluation.hpp>
#include <parsers/GeneSpeciesMapping.hpp>
#include <maths/ScaledValue.hpp>

using namespace std;

/*
* Implement the undated model described here:
* https://github.com/ssolo/ALE/blob/master/misc/undated.pdf
* In this implementation, we do not allow transfers, which 
* allows a lot of algorithmic shortcuts
*/
class UndatedDLModel: public AbstractReconciliationModel {
public:
  UndatedDLModel();
  virtual ~UndatedDLModel();
  
  // overloaded from parent
  virtual void setRates(double dupRate, double lossRate, double transferRate = 0.0);  
  // overloaded from parent
  virtual bool implementsTransfers() {return false;}
protected:
  // overload from parent
  virtual void setInitialGeneTree(shared_ptr<pllmod_treeinfo_t> treeinfo);
  // overload from parent
  virtual void updateCLV(pll_unode_t *geneNode);
  // overload from parent
  virtual ScaledValue getRootLikelihood(pllmod_treeinfo_t &treeinfo,
    pll_unode_t *root) const;
  // overload from parent
  virtual void computeRootLikelihood(pllmod_treeinfo_t &treeinfo,
    pll_unode_t *virtualRoot);
private:
  vector<double> _PD; // Duplication probability, per species branch
  vector<double> _PL; // Loss probability, per species branch
  vector<double> _PS; // Speciation probability, per species branch
  vector<double> _uE; // Extinction probability, per species branch
  
  // uq[geneId][speciesId] = probability of a gene node rooted at a species node
  // to produce the subtree of this gene node
  vector<vector<ScaledValue> > _uq;

private:
  void computeProbability(pll_unode_t *geneNode, pll_rnode_t *speciesNode, 
      ScaledValue &proba,
      bool isVirtualRoot = false) const;
};

