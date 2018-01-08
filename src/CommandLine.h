#pragma once
#include <map>
#include <string>

#include "CommonTypes.h"

struct CommandLine
{
	std::string rootDir;
	std::string slnFile;
	std::string ninjaExe;
	std::string cmakeExe;
	bool dryRun = false;
	bool verbose = false;
	std::map<std::string, StringVector> additionalDeps;
	StringVector configs{"Release", "Debug"};// order is crucial - Release rules more prioritized on conflict.

	CommandLine(int argc,  char* argv[]);
};
