#include "ReconciliationWriter.hpp"

#include <IO/ParallelOfstream.hpp>
extern "C" {
#include <pll.h>
}

static void recursivelySaveReconciliationsNHX(pll_rtree_t *speciesTree,  pll_unode_t *node, std::vector<std::vector<Scenario::Event> > &geneToEvents, ParallelOfstream &os)
{
  if(node->next) {
    os << "(";
    recursivelySaveReconciliationsNHX(speciesTree, node->next->back, geneToEvents, os);
    os << ",";
    recursivelySaveReconciliationsNHX(speciesTree, node->next->next->back, geneToEvents, os);
    os << ")";
  } 
  if (node->label) {
    os << node->label;
  } else {
    os << "n" << node->node_index; 
  }
  os << ":" << node->length;
  auto event = geneToEvents[node->node_index].back();
  if (event.isValid()) {
    os << "[&&NHX";
    if (speciesTree->nodes[event.speciesNode]->label) {
      os << ":S=" << speciesTree->nodes[event.speciesNode]->label;
    }
    os << ":D=" << (event.type == EVENT_D ? "Y" : "N" );
    os << ":H=" << (event.type == EVENT_T || event.type == EVENT_TL ? "Y" : "N" );
    if (event.type == EVENT_T || event.type == EVENT_TL) {
      assert(speciesTree->nodes[event.speciesNode]->label);
      assert(speciesTree->nodes[event.destSpeciesNode]->label);
      os << "@" << speciesTree->nodes[event.speciesNode]->label;
      os << "@" << speciesTree->nodes[event.destSpeciesNode]->label;
    }
    os << ":B=" << node->length;
    os << "]";
  }
}
  
void ReconciliationWriter::saveReconciliationNHX(pll_rtree_t *speciesTree, 
    pll_unode_t *geneRoot, 
    std::vector<std::vector<Scenario::Event> > &geneToEvents, 
    const std::string &filename, 
    bool masterRankOnly) 
{
  ParallelOfstream os(filename, masterRankOnly);
  os << "(";
  recursivelySaveReconciliationsNHX(speciesTree, geneRoot, geneToEvents, os);
  os << ",";
  recursivelySaveReconciliationsNHX(speciesTree, geneRoot->back, geneToEvents, os);
  os << ");";
}


static void recursivelySaveSpeciesTreeRecPhyloXML(pll_rnode_t *node, std::string &indent, ParallelOfstream &os)
{
  if (!node) {
    return;
  }
  os << indent << "<clade>" << std::endl;
  indent += "\t";
  os << indent << "\t<name>" << node->label << "</name>" << std::endl;
  recursivelySaveSpeciesTreeRecPhyloXML(node->left, indent, os);
  recursivelySaveSpeciesTreeRecPhyloXML(node->right, indent, os);
  indent.pop_back();
  os << indent << "</clade>" << std::endl;
}

static void saveSpeciesTreeRecPhyloXML(pll_rtree_t *speciesTree, ParallelOfstream &os)
{
  os << "<spTree>" << std::endl;
  os << "<phylogeny>" << std::endl;
  std::string indent = "";
  
  recursivelySaveSpeciesTreeRecPhyloXML(speciesTree->root, indent, os);
  os << "</phylogeny>" << std::endl;
  os << "</spTree>" << std::endl;
}

static void writeEventRecPhyloXML(pll_unode_t *geneTree,
    pll_rtree_t *speciesTree, 
    Scenario::Event  &event,
    const Scenario::Event *previousEvent,
    std::string &indent, 
    ParallelOfstream &os)
{
  auto species = speciesTree->nodes[event.speciesNode];
  pll_rnode_t *speciesOut = 0;
  os << indent << "<eventsRec>" << std::endl;
  bool previousWasTransfer = previousEvent->type == EVENT_T || previousEvent->type == EVENT_TL;
  if (previousWasTransfer && geneTree->node_index == previousEvent->transferedGeneNode && event.type != EVENT_L) {
    auto previousEventSpeciesOut = speciesTree->nodes[previousEvent->destSpeciesNode];
    os << indent << "\t<transferBack destinationSpecies=\"" << previousEventSpeciesOut->label << "\"/>" << std::endl;
  }
  
  switch(event.type) {
  case EVENT_None:
    assert(geneTree->next == 0);
    assert(species->left == 0 && species->right == 0);
    os << indent << "\t<leaf speciesLocation=\"" << species->label << "\"/>" <<  std::endl;
    break;
  case EVENT_S: case EVENT_SL:
    os << indent << "\t<speciation speciesLocation=\"" << species->label << "\"/>" << std::endl;
    break;
  case EVENT_D:
    os << indent << "\t<duplication speciesLocation=\"" << species->label << "\"/>" << std::endl;
    break;
  case EVENT_T: case EVENT_TL:
    speciesOut = speciesTree->nodes[event.speciesNode];
    os << indent << "\t<branchingOut speciesLocation=\"" << speciesOut->label << "\"/>" << std::endl;
    break; 
  case EVENT_L:
    speciesOut = speciesTree->nodes[event.speciesNode];
    os << indent << "\t<loss speciesLocation=\"" << speciesOut->label << "\"/>" << std::endl;
    break;
  default:
    const char *eventNames[]  = {"S", "SL", "D", "T", "TL", "None", "Invalid"};
    std::cerr << "please handle " << eventNames[(unsigned int)event.type] << std::endl; 
    break;
  }
  os << indent << "</eventsRec>" << std::endl;
}

static void recursivelySaveGeneTreeRecPhyloXML(pll_unode_t *geneTree, 
    bool isVirtualRoot,
    pll_rtree_t *speciesTree, 
    std::vector<std::vector<Scenario::Event> > &geneToEvents,
    const Scenario::Event *previousEvent,
    std::string &indent,
    ParallelOfstream &os)
{
  if (!geneTree) {
    return;
  }
  auto &events = geneToEvents[geneTree->node_index];
  for (unsigned int i = 0; i < events.size() - 1; ++i) {
    os << indent << "<clade>" << std::endl;
    indent += "\t";
    auto &event = events[i];
    os << indent << "<name>" << (geneTree->label ? geneTree->label : "NULL") << "</name>" << std::endl;
    writeEventRecPhyloXML(geneTree, speciesTree, event, previousEvent, indent, os);  
    previousEvent = &event;
    if (event.type == EVENT_SL || event.type == EVENT_TL) {
      Scenario::Event loss;
      loss.type = EVENT_L;
      if (event.type == EVENT_SL) {
        auto parentSpecies = speciesTree->nodes[event.speciesNode];
        auto lostSpecies = (parentSpecies->left->node_index == event.destSpeciesNode) ? parentSpecies->right : parentSpecies->left;
        loss.speciesNode = lostSpecies->node_index;
      } else if (event.type == EVENT_TL) {
        loss.speciesNode = event.speciesNode;
      }
      indent += "\t";
      os << indent << "<clade>" << std::endl;
      os << indent << "<name>loss</name>" << std::endl;
      writeEventRecPhyloXML(geneTree, speciesTree, loss, previousEvent, indent, os);
      indent.pop_back();
      os << indent << "</clade>" << std::endl;
    } else {
      assert(false); 
    }
  }

  os << indent << "<clade>" << std::endl;
  indent += "\t";
  Scenario::Event &event = geneToEvents[geneTree->node_index].back();
  os << indent << "<name>" << (geneTree->label ? geneTree->label : "NULL") << "</name>" << std::endl;
  writeEventRecPhyloXML(geneTree, speciesTree, event, previousEvent, indent, os);  

  if (geneTree->next) {
    auto left = geneTree->next->back;
    auto right = geneTree->next->next->back;
    if (isVirtualRoot) {
      left = geneTree->next;
      right = geneTree->next->back;
    }
    recursivelySaveGeneTreeRecPhyloXML(left, false, speciesTree, geneToEvents, &event, indent, os);
    recursivelySaveGeneTreeRecPhyloXML(right, false, speciesTree, geneToEvents, &event, indent, os);
  }
  for (unsigned int i = 0; i < events.size() - 1; ++i) {
    indent.pop_back();
    os << indent << "</clade>" << std::endl;
  }
  indent.pop_back();
  os << indent << "</clade>" << std::endl;
}

static void saveGeneTreeRecPhyloXML(pll_unode_t *geneTree,
    unsigned int virtualRootIndex,
    pll_rtree_t *speciesTree,
    std::vector<std::vector<Scenario::Event> > &geneToEvents, 
    ParallelOfstream &os)
{
  os << "<recGeneTree>" << std::endl;
  os << "<phylogeny rooted=\"true\">" << std::endl;
  std::string indent;
  Scenario::Event noEvent;
  noEvent.type = EVENT_None;
  pll_unode_t virtualRoot;
  virtualRoot.next = geneTree;
  virtualRoot.node_index = virtualRootIndex;
  recursivelySaveGeneTreeRecPhyloXML(&virtualRoot, true, speciesTree, geneToEvents, &noEvent, indent, os); 
  os << "</phylogeny>" << std::endl;
  os << "</recGeneTree>" << std::endl;
}

void ReconciliationWriter::saveReconciliationRecPhyloXML(pll_rtree_t *speciesTree, 
    pll_unode_t *geneRoot, 
    unsigned int virtualRootIndex,
    std::vector<std::vector<Scenario::Event> > &geneToEvents, 
    const std::string &filename, 
    bool masterRankOnly) 
{
  ParallelOfstream os(filename, masterRankOnly);
  os << "<recPhylo " << std::endl;
  os << "\txmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << std::endl;
  os << "\txsi:schemaLocation=\"http://www.recg.org ./recGeneTreeXML.xsd\"" << std::endl;
  os << "\txmlns=\"http://www.recg.org\">" << std::endl;
  saveSpeciesTreeRecPhyloXML(speciesTree, os);
  saveGeneTreeRecPhyloXML(geneRoot, virtualRootIndex, speciesTree, geneToEvents, os);
  os << "</recPhylo>";

}

