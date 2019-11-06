#include "SpeciesTreeOptimizer.hpp"

#include <optimizers/DTLOptimizer.hpp>
#include <IO/FileSystem.hpp>
#include <routines/GeneRaxMaster.hpp>
#include <routines/Routines.hpp>
#include <algorithm>

static std::string getStepTag(bool fastMove)
{
  static std::string fastMoveString("[Species tree search - Fast moves]");
  static std::string slowMoveString("[Species tree search - Slow moves]");
  return fastMove ? fastMoveString : slowMoveString;
}

SpeciesTreeOptimizer::SpeciesTreeOptimizer(const std::string speciesTreeFile, 
    const Families &initialFamilies, 
    RecModel model,
    double supportThreshold,
    const std::string &outputDir,
    const std::string &execPath):
  _speciesTree(nullptr),
  _geneTrees(std::make_unique<PerCoreGeneTrees>(initialFamilies)),
  _currentFamilies(initialFamilies),
  _model(model),
  _outputDir(outputDir),
  _execPath(execPath),
  _geneTreeIteration(1000000000),
  _supportThreshold(supportThreshold)
{
  if (speciesTreeFile == "random") {
    _speciesTree = std::make_unique<SpeciesTree>(initialFamilies);
    _speciesTree->setGlobalRates(Parameters(0.1, 0.2, 0.1));
  } else {
    _speciesTree = std::make_unique<SpeciesTree>(speciesTreeFile);
    ratesOptimization();
  }
  _speciesTree->saveToFile(FileSystem::joinPaths(_outputDir, "starting_species_tree.newick"), true);
  std::string subsamplesPath = FileSystem::joinPaths(_outputDir, "subsamples");
  FileSystem::mkdir(FileSystem::joinPaths(_outputDir, "sub_genes_opt"), true);
  FileSystem::mkdir(subsamplesPath, true);
  saveCurrentSpeciesTreeId();
}




  
void SpeciesTreeOptimizer::rootExhaustiveSearchAux(SpeciesTree &speciesTree, 
    PerCoreGeneTrees &geneTrees, 
    RecModel model, 
    bool doOptimizeGeneTrees, 
    std::vector<unsigned int> &movesHistory, 
    std::vector<unsigned int> &bestMovesHistory, 
    double &bestLL, 
    unsigned int &visits)
{
  std::vector<unsigned int> moves;
  moves.push_back(movesHistory.back() % 2);
  moves.push_back(2 + (movesHistory.back() % 2));
  for (auto direction: moves) {
    if (SpeciesTreeOperator::canChangeRoot(speciesTree, direction)) {
      movesHistory.push_back(direction);
      SpeciesTreeOperator::changeRoot(speciesTree, direction);
      double ll = computeLikelihood(doOptimizeGeneTrees, 1);
      visits++;
      if (ll > bestLL) {
        bestLL = ll;
        bestMovesHistory = movesHistory;
      }
      rootExhaustiveSearchAux(speciesTree, 
          geneTrees, 
          model, 
          doOptimizeGeneTrees, 
          movesHistory, 
          bestMovesHistory, 
          bestLL, 
          visits);
      SpeciesTreeOperator::revertChangeRoot(speciesTree, direction);
      movesHistory.pop_back();
    }
  }
}

void SpeciesTreeOptimizer::rootExhaustiveSearch(bool doOptimizeGeneTrees)
{
  //Logger::timed << getStepTag(!doOptimizeGeneTrees) << " Trying to re-root the species tree" << std::endl;

  std::vector<unsigned int> movesHistory;
  std::vector<unsigned int> bestMovesHistory;
  double bestLL = computeLikelihood(doOptimizeGeneTrees, 1);
  unsigned int visits = 1;
  movesHistory.push_back(0);
  rootExhaustiveSearchAux(*_speciesTree, 
      *_geneTrees, 
      _model, 
      doOptimizeGeneTrees, 
      movesHistory, 
      bestMovesHistory, 
      bestLL, 
      visits); 
  movesHistory[0] = 1;
  rootExhaustiveSearchAux(*_speciesTree, 
      *_geneTrees, 
      _model, 
      doOptimizeGeneTrees, 
      movesHistory, 
      bestMovesHistory, 
      bestLL, 
      visits); 
  assert (visits == 2 * _speciesTree->getTree().getLeavesNumber() - 3);
  for (unsigned int i = 1; i < bestMovesHistory.size(); ++i) {
    SpeciesTreeOperator::changeRoot(*_speciesTree, bestMovesHistory[i]);
  }
}
  
double SpeciesTreeOptimizer::sprRound(unsigned int radius)
{
  std::vector<unsigned int> prunes;
  SpeciesTreeOperator::getPossiblePrunes(*_speciesTree, prunes);
  double bestLL = computeLikelihood(false, 1);
  for (auto prune: prunes) {
    std::vector<unsigned int> regrafts;
    SpeciesTreeOperator::getPossibleRegrafts(*_speciesTree, prune, radius, regrafts);
    for (auto regraft: regrafts) {
      unsigned int rollback = SpeciesTreeOperator::applySPRMove(*_speciesTree, prune, regraft);
      double newLL = computeLikelihood(false, 1);
      if (newLL > bestLL) {
        return newLL;
      }
      SpeciesTreeOperator::reverseSPRMove(*_speciesTree, prune, rollback);
    }
  }
  return bestLL;
}

struct EvaluatedMove {
  unsigned int prune;
  unsigned int regraft;
  double ll;
};

struct less_than_evaluatedmove
{
  inline bool operator() (const EvaluatedMove& e1, const EvaluatedMove& e2)
  {
    return e1.ll > e2.ll;
  }
};


double SpeciesTreeOptimizer::sortedSprRound(unsigned int speciesRadius, double bestLL)
{
  Logger::timed << getStepTag(false) 
    << " Starting new SPR round from tree hash=" << _speciesTree->getHash() << std::endl;
  std::vector<unsigned int> prunes;
  SpeciesTreeOperator::getPossiblePrunes(*_speciesTree, prunes);
  std::vector<EvaluatedMove> evaluatedMoves;
  std::vector<double> perRadiusBestLL;
  unsigned int maxGeneRadius = 1;
  for (unsigned int currentRadius  = 0; currentRadius <= maxGeneRadius; ++currentRadius) {
    perRadiusBestLL.push_back(computeLikelihood(true, currentRadius + 1));
  }
  Logger::timed << getStepTag(false) << "   Likelihood to beat: " <<  perRadiusBestLL.back() << std::endl;
  for (auto prune: prunes) {
    std::vector<unsigned int> regrafts;
    SpeciesTreeOperator::getPossibleRegrafts(*_speciesTree, prune, speciesRadius, regrafts);
    for (auto regraft: regrafts) {
      unsigned int rollback = SpeciesTreeOperator::applySPRMove(*_speciesTree, prune, regraft);
      EvaluatedMove em;
      em.prune = prune;
      em.regraft = regraft;
      em.ll = computeLikelihood(false, 0);
      evaluatedMoves.push_back(em);
      SpeciesTreeOperator::reverseSPRMove(*_speciesTree, em.prune, rollback);
    }
  }
  std::sort(evaluatedMoves.begin(), evaluatedMoves.end(), less_than_evaluatedmove());
  unsigned int movesToTry = 30;
  if (movesToTry > evaluatedMoves.size()) {
    movesToTry = static_cast<unsigned int>(evaluatedMoves.size());
  }
  for (unsigned int i = 0; i < movesToTry; ++i) {
    auto &em = evaluatedMoves[i];
    unsigned int rollback = SpeciesTreeOperator::applySPRMove(*_speciesTree, em.prune, em.regraft);
    bool isBetter = true;
    double newBestLL;
    for (unsigned int currentRadius = 1; currentRadius <= maxGeneRadius; ++currentRadius) {
      newBestLL = computeLikelihood(true, currentRadius + 1);
      double limit = -10;
      if (currentRadius == maxGeneRadius) {
        limit = 0;
      }
      if (newBestLL < perRadiusBestLL[currentRadius] + limit) {
        isBetter = false;
        break;
      } 
    }
    if (isBetter) {
      Logger::timed << getStepTag(false) << "   Found better tree hash=" << _speciesTree->getHash() 
        << " ll=" << newBestLL << " (previous ll = " << perRadiusBestLL.back() << ")" << std::endl;
      saveCurrentSpeciesTreeId();
      return newBestLL;
    }
    SpeciesTreeOperator::reverseSPRMove(*_speciesTree, em.prune, rollback);
  } 
  return bestLL;
}

double SpeciesTreeOptimizer::sprSearch(unsigned int radius, bool doOptimizeGeneTrees)
{
  double bestLL = computeLikelihood(doOptimizeGeneTrees, 1);
  Logger::timed << getStepTag(!doOptimizeGeneTrees) << " Starting species SPR search, radius=" 
    << radius << ", bestLL=" << bestLL << ")" <<std::endl;
  double newLL = bestLL;
  do {
    bestLL = newLL;
    if (doOptimizeGeneTrees) {
      newLL = sortedSprRound(radius, bestLL); 
    } else {
      newLL = sprRound(radius);
    }
  } while (newLL - bestLL > 0.001);
  saveCurrentSpeciesTreeId();
  return newLL;
}
  
void SpeciesTreeOptimizer::ratesOptimization()
{
  auto rates = DTLOptimizer::optimizeParametersGlobalDTL(*_geneTrees, _speciesTree->getTree(), _model);
  _speciesTree->setGlobalRates(rates);
}
  
void SpeciesTreeOptimizer::saveCurrentSpeciesTreeId(std::string name, bool masterRankOnly)
{
  saveCurrentSpeciesTreePath(FileSystem::joinPaths(_outputDir, name), masterRankOnly);
}

void SpeciesTreeOptimizer::saveCurrentSpeciesTreePath(const std::string &str, bool masterRankOnly)
{
  _speciesTree->saveToFile(str, masterRankOnly);
}

double SpeciesTreeOptimizer::optimizeGeneTrees(unsigned int radius, bool inPlace)
{
  saveCurrentSpeciesTreeId("proposal_species_tree.newick");
  std::string speciesTree = FileSystem::joinPaths(_outputDir, "proposal_species_tree.newick");
  auto recOpt = RecOpt::Simplex;
  bool perFamilyDTLRates = false;
  bool rootedGeneTree = true;
  double recWeight = 1.0;
  bool useSplitImplem = true;
  long int sumElapsedSPR = 0;
  auto rates = _speciesTree->getRatesVector();
  Logger::mute();
  std::string resultName = "proposals";
  Families families = _currentFamilies;
  GeneRaxMaster::optimizeGeneTrees(families, 
      _model, rates, _outputDir, resultName, 
      _execPath, speciesTree, recOpt, perFamilyDTLRates, rootedGeneTree, 
      _supportThreshold, recWeight, true, true, radius, _geneTreeIteration, 
        useSplitImplem, sumElapsedSPR, inPlace);
  _geneTreeIteration++;
  Logger::unmute();
  double totalLibpllLL, totalRecLL;
  Routines::gatherLikelihoods(families, totalLibpllLL, totalRecLL);
  if (inPlace) {
    ratesOptimization();
    Logger::info << "Uptimed the gene trees " 
      << totalLibpllLL + totalRecLL << " " << totalLibpllLL << " " << totalRecLL << std::endl;
  }
  return totalLibpllLL + totalRecLL;
}
  
double SpeciesTreeOptimizer::computeLikelihood(bool doOptimizeGeneTrees, unsigned int geneSPRRadius)
{
  if (doOptimizeGeneTrees && geneSPRRadius >= 1) {
    auto initialFamilies = _currentFamilies;
    auto saveRates = _speciesTree->getRatesVector();
    if (_hack.dimensions() && geneSPRRadius >= 2) {
      _speciesTree->setRatesVector(_hack);
    }
    double jointLL = optimizeGeneTrees(geneSPRRadius, false);
    ratesOptimization();
    _hack = _speciesTree->getRatesVector();
    //Logger::info << geneSPRRadius << " " << jointLL << std::endl;
    _speciesTree->setRatesVector(saveRates);
    _currentFamilies = initialFamilies;
    _geneTrees = std::make_unique<PerCoreGeneTrees>(_currentFamilies);
    return jointLL;
  }
  return _speciesTree->computeReconciliationLikelihood(*_geneTrees, _model);
}


