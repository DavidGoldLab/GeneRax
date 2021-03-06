#pragma once

extern "C" {
#include <pll.h>
#include <pllmod_algorithm.h>
#include <pll_binary.h>
#include <pll_msa.h>
#include <pll_optimize.h>
#include <pll_tree.h>
#include <pllmod_util.h>  
}

#include <string>
#include <memory>
#include <vector>

#include <util/CArrayRange.hpp>
#include <IO/Model.hpp>
#include <IO/LibpllParsers.hpp>
#include <trees/PLLUnrootedTree.hpp>

class PLLTreeInfo {
public:

  PLLTreeInfo(const std::string &newickStrOrFile,
    bool isNewickAFile,
    const std::string& alignmentFilename,
    const std::string &modelStrOrFile);
 
  // forbid copy
  PLLTreeInfo(const PLLTreeInfo &) = delete;
  PLLTreeInfo & operator = (const PLLTreeInfo &) = delete;
  PLLTreeInfo(PLLTreeInfo &&) = delete;
  PLLTreeInfo & operator = (PLLTreeInfo &&) = delete;

  pllmod_treeinfo_t *getTreeInfo() {return _treeinfo.get();}
  PLLUnrootedTree &getTree() {return *_utree;}
  Model &getModel() {return *_model;}

private:
  std::unique_ptr<pllmod_treeinfo_t, void(*)(pllmod_treeinfo_t*)> _treeinfo;
  std::unique_ptr<PLLUnrootedTree> _utree;
  std::unique_ptr<Model> _model; 
private:
  void buildFromString(const std::string &newickString,
      const std::string& alignmentFilename,
      const std::string &modelStrOrFile);
  void buildModel(const std::string &modelStrOrFile);
  void buildTree(const std::string &newickStrOrFile, 
      bool isNewickAFile, 
      const PLLSequencePtrs &sequences);
  pll_partition_t * buildPartition(const PLLSequencePtrs &sequences, 
  unsigned int *patternWeights); 
  pllmod_treeinfo_t *buildTreeInfo(const Model &model,
    pll_partition_t *partition,
    PLLUnrootedTree &utree);
  
};





