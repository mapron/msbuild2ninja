#pragma once
#include <map>
#include <set>

#include "CommonTypes.h"
#include "VariableMap.h"
#include "NinjaWriter.h"


struct VcProjectInfo
{
	std::string baseDir;
	std::string targetName;

	std::string fileName;
	std::string GUID;
	std::string projectFileData;
	std::string projectFiltersData;
	StringVector dependentGuids;
	std::vector<const VcProjectInfo*> dependentTargets;
	StringVector clCompileFiles;
	StringVector rcCompileFiles;
	struct Config {
		std::string configuration;
		std::string platform;

		VariableMap projectVariables;
		VariableMap clVariables;
		VariableMap libVariables;
		VariableMap linkVariables;
	};
	std::vector<Config> configs;
	struct CustomBuild
	{
		std::string message;
		StringVector deps;
		std::string output;
		std::string command;
	};

	struct ParsedConfig {
		std::string name;
		std::string platform;

		std::string outDir;
		std::string intDir;
		std::string targetName;
		std::string targetMainExt;
		std::string targetImportExt;

		StringVector includes;
		StringVector defines;
		StringVector flags;
		StringVector link;
		StringVector linkFlags;

		std::vector<CustomBuild> customCommands;

		std::string getOutputName() const { return targetName + targetMainExt;}
		std::string getOutputNameWithDir() const { return outDir + getOutputName(); }
		std::string getImportName() const { return targetImportExt.empty() ? "" : targetName + targetImportExt;}
		std::string getImportNameWithDir() const { return targetImportExt.empty() ? "" : intDir + getImportName();}
	};
	std::vector<ParsedConfig> parsedConfigs;


	enum class Type { Unknown, Static, Dynamic, App, Utility };
	Type type = Type::Unknown;
	void ReadVcProj();
	void WriteVcProj();
	void ParseConfigs();
	void TransformConfigs(const StringVector & configurations, const std::string &rootDir);
	void ConvertToMakefile(const std::string & ninjaBin, const StringVector & customDeps);
	void CalculateDependentTargets(const std::vector<VcProjectInfo> & allTargets);
};
using VcProjectList = std::vector<VcProjectInfo>;


std::ostream & operator << (std::ostream & os, const VcProjectInfo::Config & info);

std::ostream & operator << (std::ostream & os, const VcProjectInfo::ParsedConfig & info);

std::ostream & operator << (std::ostream & os, const VcProjectInfo & info);
