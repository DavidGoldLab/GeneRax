#include "util/Scenario.hpp"
#include <IO/Logger.hpp>
#include <IO/ReconciliationWriter.hpp>
#include <IO/ParallelOfstream.hpp>

const char *Scenario::eventNames[]  = {"S", "SL", "D", "T", "TL", "None", "Invalid"};


void Scenario::addEvent(ReconciliationEventType type, unsigned int geneNode, unsigned int speciesNode) {
  addTransfer(type, geneNode, speciesNode, INVALID);
}
  
void Scenario::addTransfer(ReconciliationEventType type, 
  unsigned int geneNode, 
  unsigned int speciesNode, 
  unsigned int destSpeciesNode)
{
  Event event;
  event.type = type;
  event.geneNode = geneNode;
  event.speciesNode = speciesNode;
  event.destSpeciesNode = destSpeciesNode;
  _events.push_back(event);
  assert(static_cast<int>(type) >= 0);
  _eventsCount[static_cast<unsigned int>(type)] ++;
  if (_geneIdToEvent.size() <= static_cast<size_t>(geneNode)) {
    _geneIdToEvent.resize(geneNode + 1);
  }
  _geneIdToEvent[geneNode] = event;
}

void Scenario::saveEventsCounts(const std::string &filename, bool masterRankOnly) {
  ParallelOfstream os(filename, masterRankOnly);
  for (unsigned int i = 0; i < static_cast<unsigned int>(EVENT_Invalid); ++i) {
    os << eventNames[i] << ":" << _eventsCount[i] << std::endl;
  }
}

void Scenario::saveReconciliation(const std::string &filename, ReconciliationFormat format, bool masterRankOnly)
{
  switch (format) {
  case NHX:
    ReconciliationWriter::saveReconciliationNHX(_speciesTree, _geneRoot, _geneIdToEvent, filename, masterRankOnly);
    break;
  case RecPhyloXML:
    ReconciliationWriter::saveReconciliationRecPhyloXML(_speciesTree, _geneRoot, _geneIdToEvent, filename, masterRankOnly);
    break;
  }
}

