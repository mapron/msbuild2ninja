#include <regex>
#include <sstream>
#include <fstream>
#include <cassert>
#include <iostream>

#include "VcProjectInfo.h"
#include "FileUtils.h"

namespace {

std::string joinVector(const StringVector & lst, char sep = ' ')
{
	std::ostringstream ss;
	ss << sep;
	for (const auto & el : lst)
		ss << el << sep;
	return ss.str();
}

}

void VcProjectInfo::ParseFilters()
{
	ByteArrayHolder data;
	const std::string filtersFile = baseDir + "/" + fileName + ".filters";
	if (!FileInfo(filtersFile).ReadFile(data))
		throw std::runtime_error("Failed to read .filters file");

	std::string filestr((const char*)data.data(), data.size());

	std::string incl = "<ClCompile Include=\"";

	size_t pos = filestr.find(incl, 0);
	while (pos != std::string::npos)
	{
		size_t posStart = pos + incl.size();
		size_t posEnd = filestr.find('"', posStart + 1);
		assert(posEnd != std::string::npos);
		const auto f = filestr.substr(posStart, posEnd - posStart);
		clCompileFiles.push_back(f);

		pos = filestr.find(incl, posEnd + 1);
	}

	//std::cout << "ParseFilters: " << targetName << std::endl;
}

void VcProjectInfo::ParseConfigs()
{
	ByteArrayHolder data;
	if (!FileInfo(baseDir + "/" + fileName).ReadFile(data))
		throw std::runtime_error("Failed to read .sln file");

	std::string filestr((const char*)data.data(), data.size());
	std::smatch res;
	std::regex exp(R"rx(<ItemDefinitionGroup Condition="'\$\(Configuration\)\|\$\(Platform\)'=='(\w+)\|(\w+)'">\s*<ClCompile>)rx");
	std::regex exp2(R"rx(<(\w+)>([^<]+)</)rx");
	std::string::const_iterator searchStart( filestr.cbegin() );
	while ( std::regex_search( searchStart, filestr.cend(), res, exp ) )
	{
		searchStart += res.position() + res.length();
		auto off  = searchStart - filestr.cbegin();
		auto next = filestr.find("</ClCompile>", off);
		Config config;
		config.configuration = res[1].str();
		config.platform      = res[2].str();

		std::string deps = filestr.substr(off, next - off);
		std::smatch res2;
		std::string::const_iterator searchStart2( deps.cbegin() );
		while ( std::regex_search( searchStart2, deps.cend(), res2, exp2 ) )
		{
			std::string key = res2[1].str();
			std::string value = res2[2].str();
			config.variables[key] = value;
			searchStart2 += res2.position() + res2.length();
		}
		configs.push_back(config);
	}
	//std::cout << "ParseConfigs: " << targetName << std::endl;
}

void VcProjectInfo::TransformConfigs(const StringVector & configurations)
{
	for (const Config & config : configs)
	{
		if (std::find(configurations.cbegin(), configurations.cend(), config.configuration) == configurations.cend())
			continue;

		ParsedConfig pc;
		pc.name = config.configuration;
		pc.includes = config.GetListValue("AdditionalIncludeDirectories");
		pc.defines = config.GetListValue("PreprocessorDefinitions");


		auto flagsProcess = [&pc, &config](const std::string & key, const std::map<std::string, std::string> & mapping) {
			std::string t = config.GetMappedValue(key, mapping);
			if (!t.empty())
				pc.flags.push_back(t);
		};

		flagsProcess("RuntimeLibrary", {{"MultiThreadedDLL", "/MD"}, {"MultiThreadedDebugDLL", "/MDd"}});
		flagsProcess("ExceptionHandling", {{"Sync", "/EHsc"}});
		flagsProcess("Optimization", {{"Disabled", "/Od"}, {"MinSpace", "/O1"}, {"MaxSpeed", "/O2"}});
		flagsProcess("DebugInformationFormat", {{"ProgramDatabase", "/Zi"}});
		flagsProcess("BasicRuntimeChecks", {{"EnableFastChecks", "/RTC1"}});
		flagsProcess("RuntimeTypeInfo", {{"true", "/GR"}});
		flagsProcess("WarningLevel", {{"Level1", "/W1"}, {"Level2", "/W2"}, {"Level3", "/W3"}});
		flagsProcess("InlineFunctionExpansion", {{"AnySuitable", "/Ob2"}, {"Disabled", "/Ob0"}});

		auto disabledWarnings = config.GetListValue("DisableSpecificWarnings");
		for (auto warn : disabledWarnings)
			pc.flags.push_back("/wd" + warn);
		auto additionalOptions = config.GetStrValue("AdditionalOptions");
		additionalOptions = std::regex_replace(additionalOptions, std::regex("\\%\\(\\w+\\)"), " ");
		if (!additionalOptions.empty())
			pc.flags.push_back(additionalOptions);
		if (config.GetBoolValue("TreatWarningAsError"))
			pc.flags.push_back("/WX");

		parsedConfigs.push_back(pc);
	}
	//std::cout << "TransformConfigs: " << targetName << std::endl;
}

void VcProjectInfo::CalculateDependentTargets(const std::vector<VcProjectInfo> & allTargets)
{
	for (const auto & depGuid : dependentGuids)
	{
		auto it = std::find_if(allTargets.cbegin(), allTargets.cend(), [depGuid](const auto & info){ return info.GUID == depGuid;});
		if (it != allTargets.cend())
		{
			if (!it->clCompileFiles.empty())
				dependentTargets.push_back(it->targetName);
		}
	}
}

std::string VcProjectInfo::GetNinjaRules() const
{
	std::ostringstream ss;
	std::regex re(R"rx(\:)rx");
	std::regex re2(R"rx(\.cpp$)rx");


	for (const auto & config : this->parsedConfigs)
	{
		std::string depObjs = "";
		for (const auto & filename : this->clCompileFiles)
		{
			//std::string rulename = "CXX_" + config.name + "_" + this->targetName;
			std::string escapedName = std::regex_replace(filename, re, "$:");
			StringVector cmdDefines;
			for (const auto & def : config.defines)
				cmdDefines.push_back("/D" + def);
			StringVector cmdIncludes;
			for (const auto & inc : config.includes)
				cmdIncludes.push_back("/I" + inc);

			std::string objName = escapedName.substr(escapedName.rfind("\\") + 1);
			objName =  std::regex_replace(objName, re2, ".obj");
			std::string fullObjName = targetName + ".dir\\" + config.name + "\\" + objName;
			depObjs += " " + fullObjName;

			ss << "build " << fullObjName << ": CXX_COMPILER " << escapedName << "\n"
			  << "  FLAGS = " << joinVector(config.flags) << "\n";

			ss << "  DEFINES = " << joinVector(cmdDefines) << "\n";
			ss << "  INCLUDES = " << joinVector(cmdIncludes) << "\n";
			ss << "  TARGET_COMPILE_PDB = " << targetName << ".dir\\" << config.name << "\\" << targetName << ".pdb\n";
		}
		std::string libTargetName = config.name + "_" + targetName;
		for (const auto & dep : this->dependentTargets)
			depObjs += " " + config.name + "_" + dep;
		ss << "\nbuild " << libTargetName << ": phony " << depObjs << "\n";
	}
	return ss.str();
}

std::string VcProjectInfo::Config::GetStrValue(const std::string & key) const
{
	auto it = variables.find(key);
	if (it == variables.cend())
		return "";
	return it->second;
}

StringVector VcProjectInfo::Config::GetListValue(const std::string & key) const
{
	std::string val = GetStrValue(key);
	std::stringstream ss;
	ss.str(val);
	std::string item;
	StringVector result;
	while (std::getline(ss, item, ';'))
	{
		if (!item.empty() && item.at(0) != '%')
		{
			result.push_back(item);
		}
	}
	return result;
}

std::string VcProjectInfo::Config::GetMappedValue(const std::string & key, const std::map<std::string, std::string> & mapping) const
{
	std::string val = GetStrValue(key);
	auto it = mapping.find(val);
	if (it == mapping.cend())
		return "";

	return it->second;
}

bool VcProjectInfo::Config::GetBoolValue(const std::string & key, bool def) const
{
	std::string val = GetStrValue(key);
	if (val == "true")
		return true;

	if (val == "")
		return def;

	return false;
}
