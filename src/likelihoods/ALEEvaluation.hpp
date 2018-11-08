
#ifndef JOINTSEARCH_ALEEVALUATION_HPP
#define JOINTSEARCH_ALEEVALUATION_HPP

#include <parsers/GeneSpeciesMapping.hpp>
#include <likelihoods/ale/AbstractReconciliationModel.hpp>
#include <memory>

using namespace std;

class ALEEvaluation {

public:
  ALEEvaluation(pll_rtree_t *speciesTree,
    const GeneSpeciesMapping& map,
    bool transfers = false);

  void setRates(double dupRate, double lossRate, 
    double transfers = 0.0);

  pll_unode_t * getRoot() {return reconciliationModel->getRoot();}
  void setRoot(pll_unode_t *root) {reconciliationModel->setRoot(root);}
  double evaluate(shared_ptr<pllmod_treeinfo_t> treeinfo);
private:
  shared_ptr<AbstractReconciliationModel> reconciliationModel;
  bool firstCall;
};


#endif //TREERECS_ALEEVALUATION_H
