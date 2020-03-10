
#include "GeneRaxCore.hpp"
#include "GeneRaxInstance.hpp"
#include <parallelization/ParallelContext.hpp>
#include <IO/FamiliesFileParser.hpp>
#include <IO/Logger.hpp>
#include <IO/LibpllParsers.hpp>
#include <algorithm>
#include <random>
#include <limits>
#include <parallelization/PerCoreGeneTrees.hpp>
#include <optimizers/DTLOptimizer.hpp>
#include <maths/Parameters.hpp>
#include <IO/FileSystem.hpp>
#include <IO/ParallelOfstream.hpp>
#include <NJ/NeighborJoining.hpp>
#include <parallelization/Scheduler.hpp>
#include <routines/Routines.hpp>
#include <optimizers/SpeciesTreeOptimizer.hpp>
#include <trees/SpeciesTree.hpp>



void GeneRaxCore::initInstance(GeneRaxInstance &instance) 
{
  srand(static_cast<unsigned int>(instance.args.seed));
  FileSystem::mkdir(instance.args.output, true);
  Logger::initFileOutput(FileSystem::joinPaths(instance.args.output, "generax"));
  assert(ParallelContext::isRandConsistent());
  instance.args.printCommand();
  instance.args.printSummary();
  instance.initialFamilies = FamiliesFileParser::parseFamiliesFile(instance.args.families);
  instance.speciesTree = FileSystem::joinPaths(instance.args.output, "startingSpeciesTree.newick");
  Logger::timed << "Filtering invalid families..." << std::endl;
  bool needAlignments = instance.args.optimizeGeneTrees;
  Family::filterFamilies(instance.initialFamilies, instance.speciesTree, needAlignments, false);
  if (instance.args.speciesTree == "random") {
    Logger::timed << "Generating random starting species tree" << std::endl;
    SpeciesTree speciesTree(instance.initialFamilies);
    speciesTree.saveToFile(instance.speciesTree, true);
  } else if (instance.args.speciesTree == "NJ") {
    Logger::timed << "Generating NJ species tree" << std::endl;
    if (ParallelContext::getRank() == 0) {
      auto startingNJTree = NeighborJoining::countProfileNJ(instance.initialFamilies); 
      startingNJTree->save(instance.speciesTree);
    }
  } else if (instance.args.speciesTree == "NJst") {
    Logger::timed << "Generating NJst species tree" << std::endl;
    if (ParallelContext::getRank() == 0) {
      auto startingNJTree = NeighborJoining::geneTreeNJ(instance.initialFamilies); 
      startingNJTree->save(instance.speciesTree);
    }
  } else {
    LibpllParsers::labelRootedTree(instance.args.speciesTree, instance.speciesTree);
  }
  ParallelContext::barrier();
  Logger::timed << "Filtering invalid families based on the starting species tree..." << std::endl;
  Family::filterFamilies(instance.initialFamilies, instance.speciesTree, needAlignments, true);
  if (!instance.initialFamilies.size()) {
    Logger::info << "[Error] No valid families! Aborting GeneRax" << std::endl;
    ParallelContext::abort(10);
  }
  instance.currentFamilies = instance.initialFamilies;
  initFolders(instance);
}

void GeneRaxCore::initRandomGeneTrees(GeneRaxInstance &instance)
{
  assert(ParallelContext::isRandConsistent());
  instance.currentFamilies = instance.initialFamilies;
  bool randoms = Routines::createRandomTrees(instance.args.output, instance.currentFamilies); 
  if (randoms) {
    initialGeneTreeSearch(instance);
  }
}
  
void GeneRaxCore::printStats(GeneRaxInstance &instance)
{
  std::string coverageFile = FileSystem::joinPaths(instance.args.output,
      std::string("perSpeciesCoverage.txt"));
  Logger::timed << "Gathering statistics about the families..." << std::endl;
  Family::printStats(instance.currentFamilies, 
      instance.speciesTree,
      coverageFile);
}


static void speciesTreeSearchAux(GeneRaxInstance &instance, int samples)
{
  Families saveFamilies = instance.currentFamilies;

  if (samples > 0) {
    auto rng = std::default_random_engine {};
    std::shuffle(instance.currentFamilies.begin(), instance.currentFamilies.end(), rng);
    instance.currentFamilies.resize(samples);
  }

  ParallelContext::barrier();
  Parameters startingRates;
  switch (instance.recModel) {
  case RecModel::UndatedDL:
    startingRates = Parameters(instance.args.dupRate, instance.args.lossRate);
  break;
    case RecModel::UndatedDTL:
    startingRates = Parameters(instance.args.dupRate, instance.args.lossRate, instance.args.transferRate);
    break;
  case RecModel::UndatedIDTL:
    startingRates = Parameters(instance.args.dupRate, instance.args.lossRate, instance.args.transferRate, 0.1);
    break;
  }
  SpeciesTreeOptimizer speciesTreeOptimizer(instance.speciesTree, instance.currentFamilies, 
      instance.recModel, startingRates, instance.args.perFamilyDTLRates, instance.args.userDTLRates, instance.args.pruneSpeciesTree, instance.args.supportThreshold, 
      instance.args.output, instance.args.exec);
  if (instance.args.speciesFastRadius > 0) {
    Logger::info << std::endl;
    Logger::timed << "Start optimizing the species tree with fixed gene trees (on " 
      << instance.currentFamilies.size() << " families " << std::endl;
  }
  switch (instance.args.speciesStrategy) {
  case SpeciesSearchStrategy::SPR:
    for (unsigned int radius = 1; radius <= instance.args.speciesFastRadius; ++radius) {
      speciesTreeOptimizer.optimizeDTLRates();
      speciesTreeOptimizer.sprSearch(radius);
      speciesTreeOptimizer.rootExhaustiveSearch(false);
      instance.totalRecLL = speciesTreeOptimizer.getReconciliationLikelihood();
    }
    break;
  case SpeciesSearchStrategy::TRANSFERS:
    for (unsigned int i = 0; i < 3; ++i) {
      speciesTreeOptimizer.optimizeDTLRates();
      speciesTreeOptimizer.transferSearch();
      instance.totalRecLL = speciesTreeOptimizer.getReconciliationLikelihood();
    }
    break;
  case SpeciesSearchStrategy::HYBRID:
    for (unsigned int i = 0; i < 2; ++i) {
      speciesTreeOptimizer.optimizeDTLRates();
      speciesTreeOptimizer.transferSearch();
      speciesTreeOptimizer.sprSearch(1);
      instance.totalRecLL = speciesTreeOptimizer.getReconciliationLikelihood();
    }
    break;
  }
  speciesTreeOptimizer.saveCurrentSpeciesTreePath(instance.speciesTree, true);
  if (instance.args.speciesSlowRadius > 0) {
    Logger::info << std::endl;
    Logger::timed << "Start optimizing the species tree and gene trees together" << std::endl;
  }
  if (instance.args.speciesSlowRadius) {
    speciesTreeOptimizer.sprSearch(instance.args.speciesSlowRadius, true);
  }
  instance.totalLibpllLL = speciesTreeOptimizer.getLibpllLikeliohood();
  instance.totalRecLL = speciesTreeOptimizer.getReconciliationLikelihood();
  instance.rates = speciesTreeOptimizer.getGlobalRates();
  Logger::timed << "End of optimizing the species tree" << std::endl;
  Logger::info << "joint ll = " << instance.totalLibpllLL + instance.totalRecLL << std::endl;
  speciesTreeOptimizer.saveCurrentSpeciesTreePath(instance.speciesTree, true);

  instance.currentFamilies = saveFamilies;
  ParallelContext::barrier();
}

void GeneRaxCore::speciesTreeSearch(GeneRaxInstance &instance)
{
  assert(ParallelContext::isRandConsistent());
  if (!instance.args.optimizeSpeciesTree) {
    return;
  }
  Logger::info << "Saving tree to " << instance.speciesTree << std::endl;
  if (instance.args.speciesInitialFamiliesSubsamples > 0) {
    speciesTreeSearchAux(instance, instance.args.speciesInitialFamiliesSubsamples);
  }
  speciesTreeSearchAux(instance, -1);
}


void GeneRaxCore::geneTreeJointSearch(GeneRaxInstance &instance)
{
  assert(ParallelContext::isRandConsistent());
  if (!instance.args.optimizeGeneTrees) {
    return;
  }
  for (unsigned int i = 1; i <= instance.args.recRadius; ++i) { 
    bool enableLibpll = false;
    bool perSpeciesDTLRates = false;
    optimizeRatesAndGeneTrees(instance, perSpeciesDTLRates, enableLibpll, i);
  }
  for (unsigned int i = 1; i <= instance.args.maxSPRRadius; ++i) {
    bool enableLibpll = true;
    bool perSpeciesDTLRates = instance.args.perSpeciesDTLRates && (i >= instance.args.maxSPRRadius - 1); // only apply per-species optimization at the two last rounds
    optimizeRatesAndGeneTrees(instance, perSpeciesDTLRates, enableLibpll, i);
  }
}



void GeneRaxCore::reconcile(GeneRaxInstance &instance)
{
  assert(ParallelContext::isRandConsistent());
  if (instance.args.reconcile || instance.args.reconciliationSamples > 0) {
    Logger::timed << "Reconciling gene trees with the species tree..." << std::endl;
    ModelParameters modelRates(instance.rates, instance.recModel, false, 1);
    Routines::inferReconciliation(instance.speciesTree, instance.currentFamilies, 
      modelRates, instance.args.output, instance.args.reconcile,
      instance.args.reconciliationSamples);
    if (instance.args.buildSuperMatrix) {
      std::string outputSuperMatrix = FileSystem::joinPaths(
          instance.args.output, "superMatrix.fasta");
      Routines::computeSuperMatrixFromOrthoGroups(instance.speciesTree,
        instance.currentFamilies,
        instance.args.output, 
        outputSuperMatrix,
        true);
    }
  }
}
  
void GeneRaxCore::terminate(GeneRaxInstance &instance)
{
  assert(ParallelContext::isRandConsistent());
  Logger::timed << "Terminating the instance.." << std::endl;
  ParallelOfstream os(FileSystem::joinPaths(instance.args.output, "stats.txt"));
  os << "JointLL: " << instance.totalLibpllLL + instance.totalRecLL << std::endl;
  os << "LibpllLL: " << instance.totalLibpllLL << std::endl;
  os << "RecLL: " << instance.totalRecLL;
  Logger::info << std::endl;
  auto &rates = instance.rates;
  if (!instance.args.perFamilyDTLRates) {
    if (rates.dimensions() == 2) {
      Logger::timed << "DT rates: D=" << rates[0] << " L= " << rates[1] << std::endl;
    } else if (instance.rates.dimensions() == 3) {
      Logger::timed<< "DTL rates: D=" << rates[0] << " L= " << rates[1] << " T=" << rates[2] << std::endl;
    }
  }
  Logger::timed << "Reconciliation likelihood: " << instance.totalRecLL << std::endl;
  if (instance.totalLibpllLL) {
    Logger::timed << "Phylogenetic likelihood: " << instance.totalLibpllLL << std::endl;
    Logger::timed << "Joint likelihood: " << instance.totalLibpllLL + instance.totalRecLL << std::endl;
  }
#ifdef PRINT_TIMES
  if (instance.elapsedRaxml) {
    Logger::timed << "Initial time spent on optimizing random trees: " << instance.elapsedRaxml << "s" << std::endl;
  }
  Logger::timed << "Time spent on optimizing rates: " << instance.elapsedRates << "s" << std::endl;
  Logger::timed << "Time spent on optimizing gene trees: " << instance.elapsedSPR << "s" << std::endl;
#endif
  Logger::timed << "Results directory: " << instance.args.output << std::endl;
  Logger::timed << "End of GeneRax execution" << std::endl;
}


void GeneRaxCore::initFolders(GeneRaxInstance &instance) 
{
  assert(ParallelContext::isRandConsistent());
  std::string results = FileSystem::joinPaths(instance.args.output, "results");
  std::string proposals = FileSystem::joinPaths(instance.args.output, "proposals");
  FileSystem::mkdir(results, true);
  if (instance.args.optimizeSpeciesTree) {
    FileSystem::mkdir(proposals, true);
  }
  for (auto &family: instance.currentFamilies) {
    FileSystem::mkdir(FileSystem::joinPaths(results, family.name), true);
    if (instance.args.optimizeSpeciesTree) {
      FileSystem::mkdir(FileSystem::joinPaths(proposals, family.name), true);
    }
  }
}

void GeneRaxCore::initialGeneTreeSearch(GeneRaxInstance &instance)
{
  assert(ParallelContext::isRandConsistent());
  Logger::info << std::endl;
  Logger::timed << "[Initialization] Initial optimization of the starting random gene trees" << std::endl;
  Logger::timed << "[Initialization] All the families will first be optimized with sequences only" << std::endl;
  Logger::mute();
  Routines::runRaxmlOptimization(instance.currentFamilies, instance.args.output, 
      instance.args.execPath, instance.currentIteration++, 
      ParallelContext::allowSchedulerSplitImplementation(), instance.elapsedRaxml);
  Logger::unmute();
  Routines::gatherLikelihoods(instance.currentFamilies, instance.totalLibpllLL, instance.totalRecLL);
  Logger::timed << "[Initialization] Finished optimizing some of the gene trees" << std::endl;
  Logger::info << std::endl;
}

void GeneRaxCore::optimizeRatesAndGeneTrees(GeneRaxInstance &instance,
    bool perSpeciesDTLRates,
    bool enableLibpll,
    unsigned int sprRadius)
{
  assert(ParallelContext::isRandConsistent());
  long elapsed = 0;
  if (!instance.args.perFamilyDTLRates) {
    Logger::timed << "Reconciliation rates optimization... " << std::endl;
    Routines::optimizeRates(instance.args.userDTLRates, instance.speciesTree, instance.recModel,
      instance.args.rootedGeneTree, instance.args.pruneSpeciesTree, 
      instance.currentFamilies, perSpeciesDTLRates, instance.rates, instance.elapsedRates);
    if (instance.rates.dimensions() <= 3) {
      Logger::info << instance.rates << std::endl;
    } else {
      Logger::info << "\tRecLL=" << instance.rates.getScore() << std::endl;
    }
    Logger::info << std::endl;
  }
  std::string additionalMsg;
  if (instance.args.perFamilyDTLRates) {
    additionalMsg = std::string("reconciliation rates and ");
  }
  Logger::timed << "Optimizing " + additionalMsg + "gene trees with radius=" << sprRadius << "... " << std::endl; 
  Routines::optimizeGeneTrees(instance.currentFamilies, instance.recModel, instance.rates, 
      instance.args.output, "results", instance.args.execPath, instance.speciesTree, 
      RecOpt::Grid, instance.args.perFamilyDTLRates, 
      instance.args.rootedGeneTree, instance.args.supportThreshold, 
      instance.args.recWeight, true, enableLibpll, sprRadius, 
      instance.currentIteration++, ParallelContext::allowSchedulerSplitImplementation(), elapsed);
  instance.elapsedSPR += elapsed;
  Routines::gatherLikelihoods(instance.currentFamilies, instance.totalLibpllLL, instance.totalRecLL);
  Logger::info << "\tJointLL=" << instance.totalLibpllLL + instance.totalRecLL 
    << " RecLL=" << instance.totalRecLL << " LibpllLL=" << instance.totalLibpllLL << std::endl;
  Logger::info << std::endl;
}

