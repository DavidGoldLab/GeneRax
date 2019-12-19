
#include "Routines.hpp"
#include <sstream>
#include <IO/Logger.hpp>
#include <IO/LibpllParsers.hpp>
#include <trees/PerCoreGeneTrees.hpp>
#include <trees/SpeciesTree.hpp>
#include <optimizers/DTLOptimizer.hpp>
#include <parallelization/ParallelContext.hpp>
#include <IO/FileSystem.hpp>
#include <likelihoods/LibpllEvaluation.hpp>
#include <trees/PLLRootedTree.hpp>
#include <util/Scenario.hpp>

void Routines::optimizeRates(bool userDTLRates, 
    const std::string &speciesTreeFile,
    RecModel recModel,
    bool rootedGeneTree,
    bool pruneSpeciesTree,
    Families &families,
    bool perSpeciesRates, 
    Parameters &rates,
    long &sumElapsed) 
{
  if (userDTLRates) {
    return;
  }
  auto start = Logger::getElapsedSec();
  PerCoreGeneTrees geneTrees(families);
  bool ok = geneTrees.checkMappings(speciesTreeFile); 
  if (!ok) {
    Logger::info << "INVALID MAPPINGS" << std::endl;
    ParallelContext::abort(42);
  }
  PLLRootedTree speciesTree(speciesTreeFile);
  PerCoreEvaluations evaluations;
  buildEvaluations(geneTrees, speciesTree, recModel, rootedGeneTree, pruneSpeciesTree, evaluations);
  if (perSpeciesRates) {
    rates = DTLOptimizer::optimizeParametersPerSpecies(evaluations, speciesTree.getNodesNumber());
  } else {
    rates = DTLOptimizer::optimizeParametersGlobalDTL(evaluations);
  }
  ParallelContext::barrier(); 
  auto elapsed = (Logger::getElapsedSec() - start);
  sumElapsed += elapsed;
}


static std::string getSpeciesEventCountFile(const std::string &outputDir, const std::string &familyName)
{
  return FileSystem::joinPaths(outputDir, 
      FileSystem::joinPaths("reconciliations", familyName + "_speciesEventCounts.txt"));
}

static std::string getTransfersFile(const std::string &outputDir, const std::string &familyName, int sample = -1)
{
  auto res = FileSystem::joinPaths(outputDir, FileSystem::joinPaths("reconciliations", familyName));
  if (sample >= 0) {
    res += std::string("_") + std::to_string(sample);
  }
  res += std::string("_transfers.txt");
  return res;
}

void Routines::inferReconciliation(
    const std::string &speciesTreeFile,
    Families &families,
    RecModel model,
    const Parameters &rates,
    const std::string &outputDir,
    bool bestReconciliation,
    unsigned int reconciliationSamples,
    bool saveTransfersOnly
    )
{
  auto consistentSeed = rand();
  ParallelContext::barrier();
  PLLRootedTree speciesTree(speciesTreeFile);
  PerCoreGeneTrees geneTrees(families);
  std::string reconciliationsDir = FileSystem::joinPaths(outputDir, "reconciliations");
  FileSystem::mkdir(reconciliationsDir, true);
  std::vector<double> dup_count(speciesTree.getNodesNumber(), 0.0);
  ParallelContext::barrier();
  ParallelContext::barrier();
  for (auto &tree: geneTrees.getTrees()) {
    if (bestReconciliation) {
      std::string eventCountsFile = FileSystem::joinPaths(reconciliationsDir, tree.name + "_eventCounts.txt");
      std::string speciesEventCountsFile = getSpeciesEventCountFile(outputDir, tree.name);
      std::string transfersFile = getTransfersFile(outputDir, tree.name);
      std::string treeWithEventsFileNHX = FileSystem::joinPaths(reconciliationsDir, tree.name + "_reconciliated.nhx");
      std::string treeWithEventsFileRecPhyloXML = FileSystem::joinPaths(reconciliationsDir, 
          tree.name + "_reconciliated.xml");
      Scenario scenario;
      ReconciliationEvaluation evaluation(speciesTree, *tree.geneTree, tree.mapping, model, true);
      evaluation.setRates(rates);
      evaluation.inferMLScenario(scenario);
      if (!saveTransfersOnly) {
        scenario.saveEventsCounts(eventCountsFile, false);
        scenario.savePerSpeciesEventsCounts(speciesEventCountsFile, false);
        scenario.saveReconciliation(treeWithEventsFileRecPhyloXML, ReconciliationFormat::RecPhyloXML, false);
        scenario.saveReconciliation(treeWithEventsFileNHX, ReconciliationFormat::NHX, false);
      }
      scenario.saveTransfers(transfersFile, false);
    }
    if (reconciliationSamples) {
      Scenario scenario;
      ReconciliationEvaluation evaluation(speciesTree, *tree.geneTree, tree.mapping, model, true);
      evaluation.setRates(rates);
      std::string nhxSamples = FileSystem::joinPaths(reconciliationsDir, tree.name + "_samples.nhx");
      ParallelOfstream nhxOs(nhxSamples, false);
      for (unsigned int i = 0; i < reconciliationSamples; ++i) {
        bool stochastic = true;
        evaluation.inferMLScenario(scenario, stochastic);
        std::string transfersFile = getTransfersFile(outputDir, tree.name, i);
        if (!saveTransfersOnly) {
          scenario.saveReconciliation(nhxOs, ReconciliationFormat::NHX);
        }
        scenario.saveTransfers(transfersFile, false);
        scenario.resetBlackList();
        nhxOs << "\n";
      }
    }
  }
  srand(consistentSeed);
  ParallelContext::barrier();
}


bool Routines::createRandomTrees(const std::string &geneRaxOutputDir, Families &families)
{
  std::string startingTreesDir = FileSystem::joinPaths(geneRaxOutputDir, "startingTrees");
  bool startingTreesDirCreated = false;
  auto consistentSeed = rand();
  for (auto &family: families) {
    if (family.startingGeneTree == "__random__") {
        if (!startingTreesDirCreated) {
          FileSystem::mkdir(startingTreesDir, true);
          startingTreesDirCreated = true;
        } 
        family.startingGeneTree = FileSystem::joinPaths(geneRaxOutputDir, "startingTrees");
        family.startingGeneTree = FileSystem::joinPaths(family.startingGeneTree, family.name + ".newick");
        if (ParallelContext::getRank() == 0) {
          LibpllEvaluation::createAndSaveRandomTree(family.alignmentFile, family.libpllModel, family.startingGeneTree);
        }
    }
  }
  srand(consistentSeed);
  ParallelContext::barrier();
  return startingTreesDirCreated;
}

void Routines::gatherLikelihoods(Families &families,
    double &totalLibpllLL,
    double &totalRecLL)
{
  ParallelContext::barrier();
  totalRecLL = 0.0;
  totalLibpllLL = 0.0;
  unsigned int familiesNumber = static_cast<unsigned int>(families.size());
  for (auto i = ParallelContext::getBegin(familiesNumber); i < ParallelContext::getEnd(familiesNumber); ++i) {
    auto &family = families[i];
    std::ifstream is(family.statsFile);
    double libpllLL = 0.0;
    double recLL = 0.0;
    is >> libpllLL;
    is >> recLL;
    totalRecLL += recLL;
    totalLibpllLL += libpllLL;
  }
  ParallelContext::sumDouble(totalRecLL);
  ParallelContext::sumDouble(totalLibpllLL);
}
  
void Routines::optimizeSpeciesRatesEmpirical(const std::string &speciesTreeFile,
    RecModel recModel,
    Families &families,
    Parameters &rates,
    const std::string &outputDir,
    long &sumElapsed)
{
  ParallelContext::barrier();
  auto start = Logger::getElapsedSec();
  inferReconciliation(speciesTreeFile, families, recModel, rates, outputDir, true, 0);
  SpeciesTree speciesTree(speciesTreeFile);
  std::unordered_set<std::string> labels = speciesTree.getTree().getLabels(false);
  std::unordered_map<std::string, std::vector<double> > frequencies;
  std::vector<double> defaultFrequences(4, 0.0);
  for (auto &label: labels) {
    frequencies.insert(std::pair<std::string, std::vector<double>>(label, defaultFrequences));
  }
  for (auto &family: families) {
    std::string speciesEventCountsFile = getSpeciesEventCountFile(outputDir, family.name);
    std::ifstream is(speciesEventCountsFile);
    assert(is.good());
    std::string line;
    while (std::getline(is, line))
    {
      std::istringstream iss(line);
      std::string label;
      iss >> label;
      auto &speciesFreq = frequencies[label];
      for (unsigned int i = 0; i < 4; ++i) {
        double temp;
        iss>> temp;
        speciesFreq[i] += temp;
      }
    }
  }
  unsigned int perSpeciesFreeParameters = Enums::freeParameters(recModel); 
  rates = Parameters(speciesTree.getTree().getNodesNumber() * perSpeciesFreeParameters);
  for (auto speciesNode: speciesTree.getTree().getNodes()) {
    auto &speciesFreq = frequencies[speciesNode->label];
    double S = speciesFreq[0] + 1.0;
    for (unsigned int j = 0; j < perSpeciesFreeParameters; ++j) {
      unsigned int ratesIndex = speciesNode->node_index * perSpeciesFreeParameters + j;
      rates[ratesIndex] = (speciesFreq[j + 1] + 1.0) / S;
    }
  }
  auto elapsed = (Logger::getElapsedSec() - start);
  sumElapsed += elapsed;
  ParallelContext::barrier();
}

static const std::string keyDelimiter("-_-");

static std::string getTransferKey(const std::string &label1, const std::string &label2)
{
  return label1 + keyDelimiter + label2; 
}

void Routines::getLabelsFromTransferKey(const std::string &key, std::string &label1, std::string &label2)
{
  auto pos = key.find(keyDelimiter);
  label1 = key.substr(0, pos);
  label2 = key.substr(pos + keyDelimiter.size());
}

void Routines::getTransfersFrequencies(const std::string &speciesTreeFile,
    RecModel recModel,
    Families &families,
    const Parameters &rates,
    TransferFrequencies &transferFrequencies,
    const std::string &outputDir)
{
  int samples = 5;
  inferReconciliation(speciesTreeFile, families, recModel, rates, outputDir, false, samples, true);
  
  SpeciesTree speciesTree(speciesTreeFile);
  
  for (int i = 0; i < samples; ++i) {
    for (auto &family: families) {
      std::string transfersFile = getTransfersFile(outputDir, family.name, i);
      std::string line;
      std::ifstream is(transfersFile);
      if (!is.good()) {
        Logger::info << "ERROR " << transfersFile << std::endl;
      }
      assert(is.good());
      while (std::getline(is, line))
      {
        std::istringstream iss(line);
        std::string label1;
        std::string label2;
        iss >> label1 >> label2;
        std::string key = getTransferKey(label1, label2);
        if (transferFrequencies.end() == transferFrequencies.find(key)) {
          transferFrequencies.insert(std::pair<std::string, unsigned int>(key, 1));
        } else {
          transferFrequencies[key]++;
        }
      }
    }
  }
  ParallelOfstream os(FileSystem::joinPaths(outputDir, "transfers.txt"));
  for (auto &freq: transferFrequencies) {
    os << freq.first << " " << freq.second << std::endl;
  }
  Logger::timed <<"Finished writing transfers frequencies" << std::endl;
  ParallelContext::barrier();
}


void Routines::getParametersFromTransferFrequencies(const std::string &speciesTreeFile,
  const TransferFrequencies &frequencies, 
  Parameters &parameters)
{
  SpeciesTree speciesTree(speciesTreeFile);
  std::unordered_map<std::string, unsigned int> labelsToIds;
  speciesTree.getLabelsToId(labelsToIds);
  unsigned int species = speciesTree.getTree().getNodesNumber();
  parameters = Parameters(species * species);
  for (auto &frequency: frequencies) {
    std::string label1;
    std::string label2;
    getLabelsFromTransferKey(frequency.first, label1, label2);
    unsigned int id1 = labelsToIds[label1];
    unsigned int id2 = labelsToIds[label2];
    parameters[id1 * species + id2] = frequency.second;

  }
  
}




void Routines::buildEvaluations(PerCoreGeneTrees &geneTrees, 
    PLLRootedTree &speciesTree, 
    RecModel recModel, 
    bool rootedGeneTree, 
    bool pruneSpeciesTree, 
    Evaluations &evaluations)
{
  auto &trees = geneTrees.getTrees();
  evaluations.resize(trees.size());
  for (unsigned int i = 0; i < trees.size(); ++i) {
    auto &tree = trees[i];
    evaluations[i] = std::make_shared<ReconciliationEvaluation>(speciesTree, 
        *tree.geneTree, 
        tree.mapping, 
        recModel, 
        rootedGeneTree, 
        pruneSpeciesTree);
  }
}



