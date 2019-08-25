#pragma once

#include "util/enums.hpp"
#include <string>


  

class SpeciesRaxArguments {
public:
   SpeciesRaxArguments(int argc, char * argv[]);
   void checkInputs();
   void printHelp();
   void printCommand();
   void printSummary();
public:
   int argc;
   char ** argv;
   unsigned int seed;
   std::string families;
   std::string speciesTree;
   RecModel reconciliationModel;
   RecOpt reconciliationOpt;
   std::string output;
   SpeciesRaxStrategy strategy;
   bool perSpeciesDTLRates;
   bool rootedGeneTree;
   bool userDTLRates;
   double dupRate;
   double lossRate;
   double transferRate;
   unsigned int fastRadius;
   unsigned int slowRadius;
   unsigned int finalGeneRadius;
};

