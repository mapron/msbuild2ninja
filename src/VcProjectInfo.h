#pragma once
#include <map>

#include "CommonTypes.h"

struct VcProjectInfo
{
	std::string baseDir;
	std::string targetName;
	std::string fileName;
	std::string GUID;
	StringVector dependentGuids;
	StringVector dependentTargets;
	StringVector clCompileFiles;
	struct Config {
		std::string configuration;
		std::string platform;
		std::map<std::string, std::string> variables;
		std::string GetStrValue(const std::string & key) const;
		StringVector GetListValue(const std::string & key) const;
		std::string GetMappedValue(const std::string & key, const std::map<std::string, std::string> & mapping) const;
		bool GetBoolValue(const std::string & key, bool def = false) const;
	};
	std::vector<Config> configs;
	struct ParsedConfig {
		std::string name;
		StringVector includes;
		StringVector defines;
		StringVector flags;
	};
	std::vector<ParsedConfig> parsedConfigs;
	void ParseFilters();
	void ParseConfigs();
	void TransformConfigs(const StringVector & configurations);
	void CalculateDependentTargets(const std::vector<VcProjectInfo> & allTargets);
	std::string GetNinjaRules() const;
};
using VcProjectList = std::vector<VcProjectInfo>;
