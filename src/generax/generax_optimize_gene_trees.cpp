#include <ParallelContext.hpp>
#include <IO/FamiliesFileParser.hpp>
#include <IO/Logger.hpp>
#include <IO/LibpllParsers.hpp>
#include <algorithm>
#include <limits>
#include <PerCoreGeneTrees.hpp>
#include <optimizers/DTLOptimizer.hpp>
#include <maths/DTLRates.hpp>
#include <treeSearch/JointTree.hpp>
#include <treeSearch/SPRSearch.hpp>
#include <IO/FileSystem.hpp>


void getTreeStrings(const string &filename, vector<string> &treeStrings) 
{
  string geneTreeString;
  if (filename == "__random__" || filename.size() == 0) {
    treeStrings.push_back("__random__");
    return;
  }
  ifstream treeStream(filename);
  while(getline(treeStream, geneTreeString)) {
    geneTreeString.erase(remove(geneTreeString.begin(), geneTreeString.end(), '\n'), geneTreeString.end());
    if (geneTreeString.empty()) {
      continue;
    }
    treeStrings.push_back(geneTreeString);
  }
}

void optimizeGeneTrees(const string &startingGeneTreeFile,
    const string &mappingFile,
    const string &alignmentFile,
    const string &speciesTreeFile,
    const string libpllModel, 
    RecModel recModel,
    RecOpt recOpt,
    bool rootedGeneTree,
    double dupRate,
    double lossRate, 
    double transferRate,
    int sprRadius,
    const string &outputGeneTree) 
{
  double totalInitialLL = 0.0;
  double totalFinalLL = 0.0;
  vector<string> geneTreeStrings;
  getTreeStrings(startingGeneTreeFile, geneTreeStrings);
  assert(geneTreeStrings.size() == 1);
  auto jointTree = make_shared<JointTree>(geneTreeStrings[0],
      alignmentFile,
      speciesTreeFile,
      mappingFile,
      libpllModel,
      recModel,
      recOpt,
      rootedGeneTree,
      false, //check
      false,
      dupRate,
      lossRate,
      transferRate
      );
  jointTree->optimizeParameters(true, false); // only optimize felsenstein likelihood
  double bestLoglk = jointTree->computeJointLoglk();
  totalInitialLL += bestLoglk;
  jointTree->printLoglk();
  Logger::info << "Initial ll = " << bestLoglk << endl;
  while(SPRSearch::applySPRRound(*jointTree, sprRadius, bestLoglk)) {} 
  totalFinalLL += bestLoglk;
  Logger::info << "Final ll = " << bestLoglk << endl;
  jointTree->save(outputGeneTree, false);
  Logger::info << "Total initial and final ll: " << totalInitialLL << " " << totalFinalLL << endl;
  ParallelContext::barrier();
}


int local_internal_main(int argc, char** argv, void* comm)
{
  //ParallelContext::init(comm); 
  
  /*
  int i = 1;
  string startingGeneTreeFile(argv[i++]);
  string mapFile(argv[i++]);
  string alignmentFile(argv[i++]);
  string speciesTreeFile(argv[i++]);
  string libpllModel(argv[i++]);
  RecModel recModel = RecModel(atoi(argv[i++])); 
  RecOpt recOpt = RecOpt(atoi(argv[i++])); 
  bool rootedGeneTree = bool(atoi(argv[i++]));
  double dupRate = double(atof(argv[i++]));
  double lossRate = double(atof(argv[i++]));
  double transferRate = double(atof(argv[i++]));
  int sprRadius = atoi(argv[i++]);
  string outputGeneTree(argv[i++]);
  optimizeGeneTrees(startingGeneTreeFile,
      mapFile,
      alignmentFile,
      speciesTreeFile,
      libpllModel,
      recModel,
      recOpt,
      rootedGeneTree,
      dupRate,
      lossRate,
      transferRate,
      sprRadius,
      outputGeneTree);
      */ 
  //ParallelContext::finalize();
  return 0;
}


extern "C" int dll_main(int argc, char** argv, void* comm)
{
  cerr << "generax submain !" << endl;
  return local_internal_main(argc, argv, comm);
}



