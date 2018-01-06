#pragma once
#include <map>

#include "CommonTypes.h"

struct VariableMap
{
    std::map<std::string, std::string> variables;
    std::string GetStrValue(const std::string & key) const;
    std::string GetStrValueFiltered(const std::string & key) const;
    StringVector GetListValue(const std::string & key) const;
    std::string GetMappedValue(const std::string & key, const std::map<std::string, std::string> & mapping) const;
    bool GetBoolValue(const std::string & key, bool def = false) const;

    std::string & operator [] (const std::string & key) { return variables[key];}

    void ParseFromXml(const std::string & blockName, const std::string & data);
};

inline std::ostream & operator << (std::ostream & os, const VariableMap & info) {
    for (const auto & dep : info.variables)
        os << "\n\t\t" << dep.first << "=" << dep.second;
    return os;
}

struct VcProjectInfo
{
	std::string baseDir;
    std::string targetName;

	std::string fileName;
	std::string GUID;
    std::string projectFileData;
	StringVector dependentGuids;
	StringVector dependentTargets;
	StringVector clCompileFiles;
	struct Config {
		std::string configuration;
		std::string platform;

        VariableMap projectVariables;
        VariableMap clVariables;
        VariableMap libVariables;
        VariableMap linkVariables;
	};
	std::vector<Config> configs;
	struct ParsedConfig {
		std::string name;
        std::string platform;
        std::string targetName;
        std::string targetOutputName;
        std::string targetOutputNameWithDir;

		StringVector includes;
		StringVector defines;
        StringVector flags;
        StringVector link;
        StringVector linkFlags;
	};
	std::vector<ParsedConfig> parsedConfigs;

    enum class Type { Unknown, Static, Dynamic, App };
    Type type = Type::Unknown;

	void ParseFilters();
	void ParseConfigs();
	void TransformConfigs(const StringVector & configurations);
    void ConvertToMakefile(const std::string & ninjaBin);
	void CalculateDependentTargets(const std::vector<VcProjectInfo> & allTargets);
	std::string GetNinjaRules() const;
};
using VcProjectList = std::vector<VcProjectInfo>;
