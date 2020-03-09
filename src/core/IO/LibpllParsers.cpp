#include "LibpllParsers.hpp"
#include <fstream>
#include <streambuf>
#include <algorithm>
#include <parallelization/ParallelContext.hpp>
#include <cstring>
#include <sstream>
#include <stack>
#include <array>

extern "C" {
#include <pll.h>
}

LibpllException::~LibpllException()
{

}

void LibpllParsers::labelRootedTree(pll_rtree_t *tree)
{
  assert(tree);
  unsigned int index = 0;
  for (unsigned int i = 0; i < tree->tip_count + tree->inner_count; ++i) {
    auto node = tree->nodes[i];
    if (!node->label) {
      auto label = std::string("species_" + std::to_string(index++));
      node->label = static_cast<char*>(malloc(sizeof(char) * (label.size() + 1)));
      std::strcpy(node->label, label.c_str());
    }
  }
}

void LibpllParsers::labelRootedTree(const std::string &unlabelledNewickFile, const std::string &labelledNewickFile)
{
  pll_rtree_t *tree = readRootedFromFile(unlabelledNewickFile);
  labelRootedTree(tree);
  saveRtree(tree->root, labelledNewickFile);
  pll_rtree_destroy(tree, free);
}

pll_utree_t *LibpllParsers::readNewickFromFile(const std::string &newickFilename)
{
  std::ifstream is(newickFilename);
  if (!is)
    throw LibpllException("Could not load open newick file ", newickFilename);

  std::string line;
  if (!std::getline(is, line)) {
    throw LibpllException("Error while reading tree (file is empty) from file: ", newickFilename); 
  }
  std::string temp;
  while (std::getline(is, temp)) {
    if (line.size() > 0) {
      throw LibpllException("Error: found more than one tree in the file: ", newickFilename);
    }
  }
  
  pll_utree_t *res = nullptr;
  try {
    res = readNewickFromStr(line);
  } catch (...) {
    throw LibpllException("Error while reading tree from file: ", newickFilename);
  }
  return res;
}


pll_utree_t *LibpllParsers::readNewickFromStr(const std::string &newickString)
{
  auto utree =  pll_utree_parse_newick_string_unroot(newickString.c_str());
  if (!utree) 
    throw LibpllException("Error while reading tree from std::string: ", newickString);
  return utree;
}

pll_rtree_t *LibpllParsers::readRootedFromFile(const std::string &newickFile)
{
  auto tree = pll_rtree_parse_newick(newickFile.c_str());
  if (!tree) {
    throw LibpllException("Error while reading tree from file: ", newickFile);
  }
  return tree;
}

pll_rtree_t *LibpllParsers::readRootedFromStr(const std::string &newickString)
{
  auto rtree =  pll_rtree_parse_newick_string(newickString.c_str());
  if (!rtree) 
    throw LibpllException("Error while reading tree from std::string: ", newickString);
  return rtree;
}
  
void LibpllParsers::saveUtree(const pll_unode_t *utree, 
  const std::string &fileName, 
  bool append)
{
  std::ofstream os(fileName, (append ? std::ofstream::app : std::ofstream::out));
  char *newick = pll_utree_export_newick_rooted(utree, 0);
  os << newick;
  os.close();
  free(newick);
}
void LibpllParsers::saveRtree(const pll_rnode_t *rtree, 
    const std::string &fileName)
{
  std::ofstream os(fileName, std::ofstream::out);
  char *newick = pll_rtree_export_newick(rtree, 0);
  os << newick;
  os.close();
  free(newick);
}
  
void LibpllParsers::getRnodeNewickString(const pll_rnode_t *rnode, std::string &newick)
{
  char *newickStr = pll_rtree_export_newick(rnode, 0);
  newick = std::string(newickStr);
  free(newickStr);
}
  
void LibpllParsers::getRtreeNewickString(const pll_rtree_t *rtree, std::string &newick)
{
  getRnodeNewickString(rtree->root, newick);
}

void rtreeHierarchicalStringAux(const pll_rnode_t *node, std::vector<bool> &lefts, std::ostringstream &os)
{
  if (!node) {
    return;
  }
  for (unsigned int i = 0; i < lefts.size(); ++i) {
    auto left = lefts[i];
    if (i == lefts.size() - 1) {
      os << "---";
    } else {
      if (left) {
        os << "|  ";
      } else {
        os << "   ";
      }
    }
  }
  os << (node->label ? node->label : "null") << std::endl;
  lefts.push_back(true);
  rtreeHierarchicalStringAux(node->left, lefts, os);
  lefts[lefts.size() - 1] = false;
  rtreeHierarchicalStringAux(node->right, lefts, os);
  lefts.pop_back();
}

void LibpllParsers::getRtreeHierarchicalString(const pll_rtree_t *rtree, std::string &newick)
{
  std::ostringstream os;
  std::vector<bool> lefts;
  rtreeHierarchicalStringAux(rtree->root, lefts, os);
  newick = os.str();
}

std::vector<unsigned int> LibpllParsers::parallelGetTreeSizes(const Families &families) 
{
  unsigned int treesNumber = static_cast<unsigned int>(families.size());
  std::vector<unsigned int> localTreeSizes((treesNumber - 1 ) / ParallelContext::getSize() + 1, 0);
  for (auto i = ParallelContext::getBegin(treesNumber); i < ParallelContext::getEnd(treesNumber); i ++) {
    pll_utree_t *tree = LibpllParsers::readNewickFromFile(families[i].startingGeneTree);
    unsigned int taxa = tree->tip_count;
    localTreeSizes[i - ParallelContext::getBegin(treesNumber)] = taxa;
    pll_utree_destroy(tree, 0);
  }
  std::vector<unsigned int> treeSizes;
  ParallelContext::concatenateUIntVectors(localTreeSizes, treeSizes);
  treeSizes.erase(remove(treeSizes.begin(), treeSizes.end(), 0), treeSizes.end());
  assert(treeSizes.size() == families.size());
  return treeSizes;
}
void LibpllParsers::fillLeavesFromUtree(pll_utree_t *utree, std::unordered_set<std::string> &leaves)
{
  for (unsigned int i = 0; i < utree->tip_count + utree->inner_count; ++i) {
    auto node = utree->nodes[i];
    if (!node->next) {
      leaves.insert(std::string(node->label));
    }
  }
}

void LibpllParsers::fillLeavesFromRtree(pll_rtree_t *rtree, std::unordered_set<std::string> &leaves)
{
  for (unsigned int i = 0; i < rtree->tip_count + rtree->inner_count; ++i) {
    auto node = rtree->nodes[i];
    if (!node->left) {
      leaves.insert(std::string(node->label));
    }
  }
}

void LibpllParsers::parseMSA(const std::string &alignmentFilename, 
    const pll_state_t *stateMap,
    PLLSequencePtrs &sequences,
    unsigned int *&weights)
{
  if (!std::ifstream(alignmentFilename.c_str()).good()) {
    throw LibpllException("Alignment file " + alignmentFilename + "does not exist");
  }
  try {
    parseFasta(alignmentFilename.c_str(),
        stateMap, sequences, weights);
  } catch (...) {
    parsePhylip(alignmentFilename.c_str(),
        stateMap, sequences,
        weights);
  }
}

void LibpllParsers::parseFasta(const char *fastaFile, 
    const pll_state_t *stateMap,
    PLLSequencePtrs &sequences,
    unsigned int *&weights)
{
  auto reader = pll_fasta_open(fastaFile, pll_map_fasta);
  if (!reader) {
    throw LibpllException("Cannot parse fasta file ", fastaFile);
  }
  char * head;
  long head_len;
  char *seq;
  long seq_len;
  long seqno;
  int length;
  while (pll_fasta_getnext(reader, &head, &head_len, &seq, &seq_len, &seqno)) {
    sequences.push_back(PLLSequencePtr(new PLLSequence(head, seq, static_cast<unsigned int>(seq_len))));
    length = static_cast<int>(seq_len);
  }
  unsigned int count = static_cast<unsigned int>(sequences.size());
  char** buffer = static_cast<char**>(malloc(static_cast<size_t>(count) * sizeof(char *)));
  assert(buffer);
  for (unsigned int i = 0; i < count; ++i) {
    buffer[i] = sequences[i]->seq;
  }
  weights = pll_compress_site_patterns(buffer, stateMap, static_cast<int>(count), &length);
  if (!weights) 
    throw LibpllException("Error while parsing fasta: cannot compress sites from ", fastaFile);
  for (unsigned int i = 0; i < count; ++i) {
    sequences[i]->len = static_cast<unsigned int>(length);
  }
  free(buffer);
  pll_fasta_close(reader);
}
  
void LibpllParsers::parsePhylip(const char *phylipFile, 
    const pll_state_t *stateMap,
    PLLSequencePtrs &sequences,
    unsigned int *&weights)
{
  assert(phylipFile);
  assert(stateMap);
  std::unique_ptr<pll_phylip_t, void (*)(pll_phylip_t*)> reader(pll_phylip_open(phylipFile, pll_map_phylip),
      pll_phylip_close);
  if (!reader) {
    throw LibpllException("Error while opening phylip file ", phylipFile);
  }
  pll_msa_t *msa = nullptr;
  // todobenoit check memory leaks when using the std::exception trick
  try {
    msa = pll_phylip_parse_interleaved(reader.get());
    if (!msa) {
      throw LibpllException("failed to parse ", phylipFile);
    }
  } catch (...) {
    std::unique_ptr<pll_phylip_t, void(*)(pll_phylip_t*)> 
      reader2(pll_phylip_open(phylipFile, pll_map_phylip), pll_phylip_close);
    msa = pll_phylip_parse_sequential(reader2.get());
    if (!msa) {
      throw LibpllException("failed to parse ", phylipFile);
    }
  }
  weights = pll_compress_site_patterns(msa->sequence, stateMap, msa->count, &msa->length);
  if (!weights) 
    throw LibpllException("Error while parsing fasta: cannot compress sites");
  for (auto i = 0; i < msa->count; ++i) {
    PLLSequencePtr seq(new PLLSequence(msa->label[i], msa->sequence[i], static_cast<unsigned int>(msa->length)));
    sequences.push_back(std::move(seq));
    // avoid freeing these buffers with pll_msa_destroy
    msa->label[i] = nullptr;
    msa->sequence[i] = nullptr;
  }
  pll_msa_destroy(msa);
}
  
std::unique_ptr<Model> LibpllParsers::getModel(const std::string &modelStrOrFilename)
{
  std::string modelStr = modelStrOrFilename;
  std::ifstream f(modelStr);
  if (f.good()) {
    getline(f, modelStr);
    modelStr = modelStr.substr(0, modelStr.find(","));
  }
  return std::make_unique<Model>(modelStr);
}

bool LibpllParsers::fillLabelsFromAlignment(const std::string &alignmentFilename, 
    const std::string& modelStrOrFilename,  
    std::unordered_set<std::string> &leaves)
{
  auto model = getModel(modelStrOrFilename);
  PLLSequencePtrs sequences;
  unsigned int *patternWeights = nullptr;
  bool res = true;
  try { 
    LibpllParsers::parseMSA(alignmentFilename, model->charmap(), sequences, patternWeights);
  } catch (...) {
    res = false;
  }
  free(patternWeights);
  for (auto &sequence: sequences) {
    leaves.insert(sequence->label);
  }
  return res;
}
  
bool LibpllParsers::areLabelsValid(std::unordered_set<std::string> &leaves)
{
  std::array<bool, 256> forbiddenCharacter{}; // all set to false
  forbiddenCharacter[';'] = true;
  forbiddenCharacter[')'] = true;
  forbiddenCharacter['('] = true;
  forbiddenCharacter['['] = true;
  forbiddenCharacter[']'] = true;
  forbiddenCharacter[','] = true;
  forbiddenCharacter[':'] = true;
  forbiddenCharacter[';'] = true;
  for (auto &label: leaves) {
    for (auto c: label) {
      if (forbiddenCharacter[c]) {
        return false;
      }
    }
  }
  return true;
}
  
void LibpllParsers::writeSuperMatrixFasta(const SuperMatrix &superMatrix,
      const std::string &outputFile)
{
  std::ofstream os(outputFile);
  for (auto &p: superMatrix) {
    auto &label = p.first;
    auto &sequence = p.second;
    os << ">" << label << std::endl;
    os << sequence << std::endl;
  }
}
  
