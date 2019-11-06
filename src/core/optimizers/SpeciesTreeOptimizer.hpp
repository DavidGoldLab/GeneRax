#pragma once

#include <trees/SpeciesTree.hpp>
#include <trees/PerCoreGeneTrees.hpp>
#include <string>
#include <maths/Parameters.hpp>
#include <util/enums.hpp>
#include <families/Families.hpp>
#include <memory>

class SpeciesTreeOptimizer {
public:
  SpeciesTreeOptimizer(const std::string speciesTreeFile, 
      const Families &initialFamilies, 
      RecModel model,
      double supportThreshold,
      const std::string &outputDir,
      const std::string &execPath);
  
  // forbid copy
  SpeciesTreeOptimizer(const SpeciesTreeOptimizer &) = delete;
  SpeciesTreeOptimizer & operator = (const SpeciesTreeOptimizer &) = delete;
  SpeciesTreeOptimizer(SpeciesTreeOptimizer &&) = delete;
  SpeciesTreeOptimizer & operator = (SpeciesTreeOptimizer &&) = delete;
  
  void setModel(RecModel model) {_model = model;}

  void rootExhaustiveSearch(bool doOptimizeGeneTrees);
  double sprRound(unsigned int radius);
  double sortedSprRound(unsigned int radius, double bestLL);
  double sprSearch(unsigned int radius, bool doOptimizeGeneTrees);
  void ratesOptimization();
  double optimizeGeneTrees(unsigned int radius, bool inPlace = false);

  double getReconciliationLikelihood() {return computeLikelihood(false);}
  double computeLikelihood(bool doOptimizeGeneTrees, unsigned int geneSPRRadius = 1);
  void saveCurrentSpeciesTreeId(std::string str = "inferred_species_tree.newick", bool masterRankOnly = true);
  void saveCurrentSpeciesTreePath(const std::string &str, bool masterRankOnly = true);
  
private:
  std::unique_ptr<SpeciesTree> _speciesTree;
  std::unique_ptr<PerCoreGeneTrees> _geneTrees;
  Families _currentFamilies;
  RecModel _model;
  std::string _outputDir;
  std::string _execPath;
  unsigned int _geneTreeIteration;
  Parameters _hack;
  double _supportThreshold;
private:
  void rootExhaustiveSearchAux(SpeciesTree &speciesTree, 
      PerCoreGeneTrees &geneTrees, 
      RecModel model, 
      bool doOptimizeGeneTrees, 
      std::vector<unsigned int> &movesHistory, 
      std::vector<unsigned int> &bestMovesHistory, 
      double &bestLL, 
      unsigned int &visits);
  std::string getSpeciesTreePath(const std::string &speciesId);
};


