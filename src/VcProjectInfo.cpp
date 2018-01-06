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
		throw std::runtime_error("Failed to read project file");

	projectFileData = std::string((const char*)data.data(), data.size());

	{
		std::smatch res;
		std::regex exp(R"rx(<ConfigurationType>(\w+)</ConfigurationType>)rx");
		std::string::const_iterator searchStart( projectFileData.cbegin() );
		if ( std::regex_search( searchStart, projectFileData.cend(), res, exp ) )
		{
			const std::string configurationType = res[1].str();
			if (configurationType == "StaticLibrary")
				type = Type::Static;
			else if (configurationType == "DynamicLibrary")
				type = Type::Dynamic;
			else if (configurationType == "Application")
				type = Type::App;
		}
	}
	if (type == Type::Unknown)
		return;

	std::map<std::string, VariableMap> projectProperties;
	{
		auto start = projectFileData.find("/_ProjectFileVersion");
		auto end   = projectFileData.find("/PropertyGroup", start);
		std::regex exp2(R"rx(<(\w+) Condition="'\$\(Configuration\)\|\$\(Platform\)'=='(\w+)\|(\w+)'">([^<]+)</)rx");
		std::string::const_iterator searchStart( projectFileData.cbegin() + start );
		std::smatch res;
		while ( std::regex_search( searchStart, projectFileData.cbegin() + end, res, exp2 ) )
		{
			searchStart += res.position() + res.length();
			std::string key = res[1].str();
			std::string configuration = res[2].str();
			std::string value = res[4].str();
			projectProperties[configuration][key] = value;
		}
	}


	std::smatch res;
	std::regex exp(R"rx(<ItemDefinitionGroup Condition="'\$\(Configuration\)\|\$\(Platform\)'=='(\w+)\|(\w+)'">\s*)rx");

	std::string::const_iterator searchStart( projectFileData.cbegin() );
	while ( std::regex_search( searchStart, projectFileData.cend(), res, exp ) )
	{
		searchStart += res.position() + res.length();
		auto off  = searchStart - projectFileData.cbegin();
		auto next = projectFileData.find("</ItemDefinitionGroup>", off);
		std::string deps = projectFileData.substr(off, next - off);

		Config config;
		config.configuration = res[1].str();
		config.platform      = res[2].str();

		config.projectVariables = projectProperties[config.configuration];

		config.clVariables.ParseFromXml("ClCompile", deps);
		config.libVariables.ParseFromXml("Lib", deps);
		config.linkVariables.ParseFromXml("Link", deps);

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
		pc.platform = config.platform;
		pc.includes = config.clVariables.GetListValue("AdditionalIncludeDirectories");
		pc.defines = config.clVariables.GetListValue("PreprocessorDefinitions");
		pc.link = config.linkVariables.GetListValue("AdditionalDependencies");
		pc.targetName = config.projectVariables.GetStrValue("TargetName");
		pc.targetOutputName = pc.targetName + config.projectVariables.GetStrValue("TargetExt");
		pc.targetOutputNameWithDir = config.configuration + "\\" + pc.targetOutputName;

		auto flagsProcess = [&pc, &config](const std::string & key, const std::map<std::string, std::string> & mapping) {
			std::string t = config.clVariables.GetMappedValue(key, mapping);
			if (!t.empty())
				pc.flags.push_back(t);
		};
		auto linkFlagsProcess = [&pc, &config](const std::string & key, const std::map<std::string, std::string> & mapping) {
			std::string t = config.linkVariables.GetMappedValue(key, mapping);
			if (!t.empty())
				pc.linkFlags.push_back(t);
		};

		flagsProcess("RuntimeLibrary", {{"MultiThreadedDLL", "/MD"}, {"MultiThreadedDebugDLL", "/MDd"}});
		flagsProcess("ExceptionHandling", {{"Sync", "/EHsc"}});
		flagsProcess("Optimization", {{"Disabled", "/Od"}, {"MinSpace", "/O1"}, {"MaxSpeed", "/O2"}});
		flagsProcess("DebugInformationFormat", {{"ProgramDatabase", "/Zi"}});
		flagsProcess("BasicRuntimeChecks", {{"EnableFastChecks", "/RTC1"}});
		flagsProcess("RuntimeTypeInfo", {{"true", "/GR"}});
		flagsProcess("WarningLevel", {{"Level1", "/W1"}, {"Level2", "/W2"}, {"Level3", "/W3"}});
		flagsProcess("InlineFunctionExpansion", {{"AnySuitable", "/Ob2"}, {"Disabled", "/Ob0"}});
		flagsProcess("CompileAs", {{"CompileAsC", "/TC"}, {"CompileAsCPP", "/TP"}});

		auto disabledWarnings = config.clVariables.GetListValue("DisableSpecificWarnings");
		for (auto warn : disabledWarnings)
			pc.flags.push_back("/wd" + warn);
		auto additionalOptions = config.clVariables.GetStrValueFiltered("AdditionalOptions");
		if (!additionalOptions.empty())
			pc.flags.push_back(additionalOptions);
		if (config.clVariables.GetBoolValue("TreatWarningAsError"))
			pc.flags.push_back("/WX");


		auto additionalOptionsLink = config.linkVariables.GetStrValueFiltered("AdditionalOptions");
		if (!additionalOptionsLink.empty())
			pc.linkFlags.push_back(additionalOptionsLink);

		additionalOptionsLink = config.libVariables.GetStrValueFiltered("AdditionalOptions");
		if (!additionalOptionsLink.empty())
			pc.linkFlags.push_back(additionalOptionsLink);

		linkFlagsProcess("GenerateDebugInformation", {{"true", "/debug"}});
		linkFlagsProcess("SubSystem", {{"Console", "/subsystem:console"}}); // @todo:  windows?

		if (config.projectVariables.GetBoolValue("LinkIncremental"))
			pc.linkFlags.push_back("/INCREMENTAL");

		parsedConfigs.push_back(pc);
	}
	//std::cout << "TransformConfigs: " << targetName << std::endl;
}

void VcProjectInfo::ConvertToMakefile(const std::string &ninjaBin)
{
	if (type == Type::Unknown)
		return;

	projectFileData = std::regex_replace(projectFileData,
										 std::regex("<ConfigurationType>\\w+</ConfigurationType>"),
										 "<ConfigurationType>Makefile</ConfigurationType>");

	const std::string refEnd = "</ProjectReference>";
	auto posRef = projectFileData.find("<ProjectReference ");
	auto posRefEnd = projectFileData.rfind(refEnd);
	projectFileData.erase(projectFileData.begin() + posRef, projectFileData.begin() + posRefEnd + refEnd.size());

	std::ostringstream os;
	for (const ParsedConfig & config : parsedConfigs)
	{
		os << "<PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" << config.name << "|" << config.platform << "'\">\n"
		   << "<NMakeBuildCommandLine>\""   << ninjaBin << "\" " << config.targetOutputNameWithDir << "</NMakeBuildCommandLine>\n"
		   << "<NMakeReBuildCommandLine>\"" << ninjaBin << "\" -t clean &amp;&amp; \"" << ninjaBin <<"\" " << config.targetOutputNameWithDir << "</NMakeReBuildCommandLine>
		   << "<NMakeCleanCommandLine>\""   << ninjaBin << "\" -t clean</NMakeCleanCommandLine>
		   << "<NMakePreprocessorDefinitions>" << joinVector(config.defines, ';') << "</NMakePreprocessorDefinitions>\n"
		   << "<NMakeIncludeSearchPath>"       << joinVector(config.includes, ';') << "</NMakeIncludeSearchPath>\n"
		   << "</PropertyGroup>\n"
			  ;
	}
	std::string nmakeProperties = os.str();
	const std::string lastPropertyGroup = "</PropertyGroup>";
	auto pos = projectFileData.rfind(lastPropertyGroup);
	projectFileData.insert(projectFileData.begin() + pos + lastPropertyGroup.size(), nmakeProperties.cbegin(), nmakeProperties.cend());

	ByteArrayHolder data;
	data.resize(projectFileData.size());
	memcpy(data.data(), projectFileData.c_str(), projectFileData.size());
	if (!FileInfo(baseDir + "/" + fileName).WriteFile(data))
		 throw std::runtime_error("Failed to write file:" + fileName);
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
		//std::string libTargetName = config.name + "_" + targetName;
		std::string depLibs;
		for (const auto & dep : this->dependentTargets)
			depLibs += " " + config.name + "\\" + dep + ".lib";

		std::string linkLibraries = joinVector(config.link);
		//ss << "\nbuild " << libTargetName << ": phony " << depObjs << "\n";
		std::string linkFlags = joinVector(config.linkFlags);
		if (type == Type::App || type == Type::Dynamic)
		{
			if (type == Type::App)
				ss << "\nbuild " << config.targetOutputNameWithDir << ": CXX_EXECUTABLE_LINKER " << depObjs << " | " << depLibs << " || " << depLibs << "\n";
			else
				ss << "\nbuild " << config.name << "\\" << config.targetName << ".lib " << config.targetOutputNameWithDir << ": CXX_SHARED_LIBRARY_LINKER " << depObjs << " | " << depLibs << " || " << depLibs << "\n";

			ss << "  FLAGS = \n"
			"  LINK_FLAGS = " << linkFlags << "\n"
			"  LINK_LIBRARIES = " << linkLibraries << "\n"
			"  OBJECT_DIR = " << targetName << ".dir\\" << config.name << "\n"
			"  POST_BUILD = cd .\n"
			"  PRE_LINK = cd .\n"
			"  TARGET_COMPILE_PDB = " << targetName << ".dir\\" << config.name << "\\ \n"
			"  TARGET_FILE = " << config.targetOutputNameWithDir << "\n"
			"  TARGET_IMPLIB = " << config.name << "\\" << config.targetName << ".lib\n"
			"  TARGET_PDB = " << config.name << "\\" << config.targetName << ".pdb\n"
				  ;
		}
		else if (type == Type::Static)
		{
			ss << "\nbuild " << config.targetOutputNameWithDir << ": CXX_STATIC_LIBRARY_LINKER " << depObjs << "\n"
		   "  LANGUAGE_COMPILE_FLAGS =\n"
		   "  LINK_FLAGS = " << linkFlags << "\n"
		   "  OBJECT_DIR = " << targetName << ".dir\\" << config.name << "\n"
		   "  POST_BUILD = cd .\n"
		   "  PRE_LINK = cd .\n"
		   "  TARGET_COMPILE_PDB = " << targetName << ".dir\\" << config.name << "\\" << config.targetName << ".pdb\n"
		   "  TARGET_FILE = " << config.targetOutputNameWithDir << "\n"
		   "  TARGET_PDB = " << config.name << "\\" << config.targetName << ".pdb\n"
				  ;
		}
	}

	return ss.str();
}

std::string VariableMap::GetStrValue(const std::string & key) const
{
	auto it = variables.find(key);
	if (it == variables.cend())
		return "";
	return it->second;
}

std::string VariableMap::GetStrValueFiltered(const std::string &key) const
{
	auto result = GetStrValue(key);
	result = std::regex_replace(result, std::regex("\\%\\(\\w+\\)"), " ");
	return result;
}

StringVector VariableMap::GetListValue(const std::string & key) const
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

std::string VariableMap::GetMappedValue(const std::string & key, const std::map<std::string, std::string> & mapping) const
{
	std::string val = GetStrValue(key);
	auto it = mapping.find(val);
	if (it == mapping.cend())
		return "";

	return it->second;
}

bool VariableMap::GetBoolValue(const std::string & key, bool def) const
{
	std::string val = GetStrValue(key);
	if (val == "true")
		return true;

	if (val == "")
		return def;

	return false;
}

void VariableMap::ParseFromXml(const std::string &blockName, const std::string &data)
{
	const auto blockBegin = data.find("<" + blockName + ">");
	if (blockBegin == std::string::npos)
		return;
	const auto blockEnd = data.find("</" + blockName + ">", blockBegin);
	if (blockEnd == std::string::npos)
		return;
	std::regex exp2(R"rx(<(\w+)>([^<]+)</)rx");
	std::smatch res2;
	std::string::const_iterator searchStart2( data.cbegin() + blockBegin );
	while ( std::regex_search( searchStart2, data.cbegin() + blockEnd, res2, exp2 ) )
	{
		const std::string key = res2[1].str();
		const std::string value = res2[2].str();
	  //  std::cerr << key << "=" << value << std::endl;
		variables[key] = value;
		searchStart2 += res2.position() + res2.length();
	}
}
