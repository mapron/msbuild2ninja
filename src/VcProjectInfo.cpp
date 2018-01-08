#include <regex>
#include <sstream>
#include <fstream>
#include <cassert>
#include <iostream>

#include "VcProjectInfo.h"
#include "FileUtils.h"

void VcProjectInfo::ReadVcProj()
{
	if (!FileInfo(baseDir + "/" + fileName).ReadFile(projectFileData))
		throw std::runtime_error("Failed to read project file");
}

void VcProjectInfo::WriteVcProj()
{
	if (!FileInfo(baseDir + "/" + fileName).WriteFile(projectFileData))
		 throw std::runtime_error("Failed to write file:" + fileName);
}

void VcProjectInfo::ParseConfigs()
{

	{
		auto parseIncludes = [this](StringVector & result, const std::string & tag)
		{
			const std::string incl = "<" + tag + " Include=\"";
			size_t pos = projectFileData.find(incl, 0);
			while (pos != std::string::npos)
			{
				size_t posStart = pos + incl.size();
				size_t posEnd = projectFileData.find('"', posStart + 1);
				assert(posEnd != std::string::npos);
				const auto f = projectFileData.substr(posStart, posEnd - posStart);
				result.push_back(f);

				pos = projectFileData.find(incl, posEnd + 1);
			}
		};
		parseIncludes(clCompileFiles, "ClCompile");
		parseIncludes(rcCompileFiles, "ResourceCompile");
	}

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
			else if (configurationType == "Utility")
				type = Type::Utility;
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

void VcProjectInfo::TransformConfigs(const StringVector & configurations, const std::string & rootDir)
{
	auto filterLinkLibraries = [](const StringVector & libs, const std::string & config) -> StringVector
	{
		StringVector result;
		std::copy_if(libs.cbegin(), libs.cend(), std::back_inserter(result), [&config](const auto & lib){
			return lib.find(config + "\\") != 0;
		});
		return result;
	};
	for (const Config & config : configs)
	{
		if (std::find(configurations.cbegin(), configurations.cend(), config.configuration) == configurations.cend())
			continue;

		ParsedConfig pc;
		pc.name = config.configuration;
		pc.platform = config.platform;
		pc.includes = config.clVariables.GetListValue("AdditionalIncludeDirectories");
		pc.defines = config.clVariables.GetListValue("PreprocessorDefinitions");
		pc.link = filterLinkLibraries(config.linkVariables.GetListValue("AdditionalDependencies"), config.configuration);
		pc.targetName = config.projectVariables.GetStrValue("TargetName");
		pc.targetMainExt = config.projectVariables.GetStrValue("TargetExt");
		pc.outDir = config.projectVariables.GetStrValue("OutDir");
		pc.intDir = config.projectVariables.GetStrValue("IntDir");
		if (pc.targetName.empty())
			pc.targetName = targetName;
		if (pc.outDir.find(rootDir) == 0)
			pc.outDir = pc.outDir.substr( rootDir.size() + 1 );
		if (type == Type::Utility) // hack to ensure target name has "Configuration_Target" format
			pc.outDir = pc.name + "_";

		pc.targetImportExt = type == Type::Dynamic ? ".lib" : "";

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

		auto ignoredLibs = config.linkVariables.GetListValue("IgnoreSpecificDefaultLibraries");
		for (auto lib: ignoredLibs)
			pc.linkFlags.push_back("/NODEFAULTLIB:" + lib);

		auto libpaths = config.linkVariables.GetListValue("AdditionalLibraryDirectories");
		for (auto path : libpaths)
			pc.linkFlags.push_back("/LIBPATH:\"" + path + "\"");

		additionalOptionsLink = config.libVariables.GetStrValueFiltered("AdditionalOptions");
		if (!additionalOptionsLink.empty())
			pc.linkFlags.push_back(additionalOptionsLink);

		linkFlagsProcess("GenerateDebugInformation", {{"true", "/debug"}, {"Debug", "/debug"}}); // wtf - cmake does this.
		linkFlagsProcess("SubSystem", {{"Console", "/subsystem:console"}, {"Windows", "/subsystem:windows"}});
		linkFlagsProcess("ImageHasSafeExceptionHandlers", {{"false", "/SAFESEH:NO"}});
		linkFlagsProcess("LargeAddressAware", {{"true", "/LARGEADDRESSAWARE"}});

		if (config.projectVariables.GetBoolValue("LinkIncremental"))
			pc.linkFlags.push_back("/INCREMENTAL");

		parsedConfigs.push_back(pc);
	}
	//std::cout << "TransformConfigs: " << targetName << std::endl;
}

void VcProjectInfo::ConvertToMakefile(const std::string &ninjaBin, const StringVector &customDeps)
{
	if (type == Type::Unknown)
		return;

	const std::string refEnd = "</ProjectReference>";
	auto posRef = projectFileData.find("<ProjectReference ");
	auto posRefEnd = projectFileData.rfind(refEnd);
	if (posRef == std::string::npos || posRefEnd == std::string::npos)
		return;

	projectFileData.erase(projectFileData.begin() + posRef, projectFileData.begin() + posRefEnd + refEnd.size());

	projectFileData = std::regex_replace(projectFileData,
										 std::regex("<ConfigurationType>\\w+</ConfigurationType>"),
										 "<ConfigurationType>Makefile</ConfigurationType>");

	std::ostringstream os;
	for (const ParsedConfig & config : parsedConfigs)
	{
		os << "<PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" << config.name << "|" << config.platform << "'\">\n"
		   << "  <NMakeBuildCommandLine>\""   << ninjaBin << "\" " << config.getOutputNameWithDir() << "</NMakeBuildCommandLine>\n"
		   << "  <NMakeReBuildCommandLine>\"" << ninjaBin << "\" -t clean &amp;&amp; \"" << ninjaBin <<"\" " << config.getOutputNameWithDir() << "</NMakeReBuildCommandLine>\n"
		   << "  <NMakeCleanCommandLine>\""   << ninjaBin << "\" -t clean</NMakeCleanCommandLine>\n"
		   << "  <NMakePreprocessorDefinitions>" << joinVector(config.defines, ';') << "</NMakePreprocessorDefinitions>\n"
		   << "  <NMakeIncludeSearchPath>"       << joinVector(config.includes, ';') << "</NMakeIncludeSearchPath>\n"
		   << "</PropertyGroup>\n"
			  ;
	}
	std::string nmakeProperties = os.str();
	const std::string lastPropertyGroup = "</PropertyGroup>";
	auto pos = projectFileData.rfind(lastPropertyGroup);
	projectFileData.insert(projectFileData.begin() + pos + lastPropertyGroup.size(), nmakeProperties.cbegin(), nmakeProperties.cend());

	auto extractParam = [this](const std::string & data, const std::string & name, const std::string & configName, const std::string & configPlatform)
	{
		const std::string startMark = "<" + name + " Condition=\"'$(Configuration)|$(Platform)'=='" + configName + "|"+ configPlatform + "'\">";
		const std::string endMark = "</" + name + ">";
		auto startPos = data.find(startMark);
		auto endPos  =  data.find(endMark, startPos);
		return data.substr(startPos + startMark.size(), endPos - startPos - startMark.size());
	};

	auto filterCommandScript = [](const std::string & data) -> std::string
	{
		auto lines = strToList(data, '\n');
		for (const auto & line : lines)
		{
			if (line.size() < 4)
				continue;
			if (line.find("if ") == 0 // @todo: regexp?
				|| line.find("cd ") == 0
				|| line.find("exit ") == 0
				|| line.find("setlocal") == 0
				|| line.find("endlocal") == 0
				|| line[0] == ':')
				continue;

			return std::regex_replace(line, std::regex("[\r\n]"), "");
		}
		return "";
	};

	auto filterCustomInputs = [](const StringVector & inputs) -> StringVector
	{
		StringVector result;
		bool ignoreAll = false;
		std::copy_if(inputs.cbegin(), inputs.cend(), std::back_inserter(result), [&ignoreAll](const auto & input){
			if (input.find("CMakeLists.txt") != std::string::npos)
				ignoreAll = true;

			if (input.find(".rule") != std::string::npos || input.find(".depends") != std::string::npos)
				return false;
			return true;
		});
		if (ignoreAll)
			return {};
		return result;
	};

	while (true)
	{
		auto startPos = projectFileData.find("<CustomBuild ");
		if (startPos == std::string::npos)
			break;

		const std::string endCustomBuild = "</CustomBuild>";
		auto endPos = projectFileData.find(endCustomBuild, startPos);
		const std::string customBuildRule = projectFileData.substr(startPos, endPos-startPos);
		projectFileData.erase(projectFileData.begin() + startPos, projectFileData.begin() + endPos + endCustomBuild.size() );

		for (ParsedConfig & config : parsedConfigs)
		{
			CustomBuild rule;
			auto extractParamConfig = [&extractParam, &customBuildRule, &config](const std::string & name) {
				return extractParam(customBuildRule, name, config.name, config.platform);
			};
			rule.message = extractParamConfig("Message");
			rule.output = extractParamConfig("Outputs");
			if (rule.output.find("generate.stamp") != std::string::npos)
				continue;
			if (rule.output.find("CMakeFiles") != std::string::npos) // hack! this allows phony rules be different on debug and release, but real outputs (e.g. resources) be the same.
				rule.output += "\\" + config.name;
			auto inputs = strToList(extractParamConfig("AdditionalInputs"));
			inputs = filterCustomInputs(inputs);
			if (inputs.empty())
				inputs = customDeps;
			if (inputs.empty())
				continue;
			rule.deps = inputs;
			rule.command = filterCommandScript(extractParamConfig("Command"));

			if (!inputs.empty())
				config.customCommands.push_back(rule);
		}
	}
}

void VcProjectInfo::CalculateDependentTargets(const std::vector<VcProjectInfo> & allTargets)
{
	for (const auto & depGuid : dependentGuids)
	{
		auto it = std::find_if(allTargets.cbegin(), allTargets.cend(), [depGuid](const auto & info){ return info.GUID == depGuid;});
		if (it != allTargets.cend())
		{
			if (!it->clCompileFiles.empty()) // @todo: maybe type check?
				dependentTargets.push_back(&*it);
		}
	}
}

std::string VcProjectInfo::GetNinjaRules(std::set<std::string> & existingRules, NinjaWriter & ninjaWriter) const
{
	std::ostringstream ss;

	if (type == Type::Unknown)
		return "";


	std::set<std::string> existingObjNames;
	auto getObjectName = [&existingObjNames](const std::string & filename, const std::string & intDir)
	{
		static std::regex re("\\.(cpp|rc)$");
		std::string objName = filename.substr(filename.rfind("\\") + 1);
		objName =  std::regex_replace(objName, re, ".obj");
		std::string fullObjName = intDir + objName;
		int prefix = 1;
		while (existingObjNames.find(fullObjName) != existingObjNames.end())
		{
			fullObjName = intDir + std::to_string(prefix++) + "_" + objName;
		}
		existingObjNames.insert(fullObjName);
		return fullObjName;
	};

	size_t configIndex = 0;
	for (const auto & config : this->parsedConfigs)
	{
		std::string orderOnlyTarget;
		std::string orderDeps;
		for (const auto & customCmd : config.customCommands)
		{
			const auto escapedOut = ninjaWriter.Escape(customCmd.output);
			if (existingRules.find(escapedOut) == existingRules.end())
			{
				if (customCmd.command.empty())
					ss << "\nbuild " << escapedOut << ": phony " << ninjaWriter.Escape(customCmd.deps) <<  "\n";
				else
					ss << "\nbuild " << escapedOut << ": CUSTOM_COMMAND " << ninjaWriter.Escape(customCmd.deps) <<  "\n"
						"  COMMAND = cmd.exe /C \"" << customCmd.command << "\" \n"
						"  DESC = " << customCmd.message << "\n"
						"  restat = 1\n"
						  ;
			}
			existingRules.insert(escapedOut);
			orderDeps += " " + escapedOut;
		}

		std::string depLink, depsTargets;
		if (!orderDeps.empty())
		{
			orderOnlyTarget = "order_only_" + config.name + "_" + config.targetName;
			ss << "\nbuild " << orderOnlyTarget << ": phony || " << orderDeps << "\n";
			depsTargets = " " + orderOnlyTarget;
		}
		for (const VcProjectInfo * dep : this->dependentTargets)
		{
			const auto & depConfig = dep->parsedConfigs[configIndex];
			assert(depConfig.name == config.name);
			if (dep->type == Type::Dynamic)
			{
				depLink     += " " + ninjaWriter.Escape(depConfig.getImportNameWithDir());
				depsTargets += " " + ninjaWriter.Escape(depConfig.getOutputNameWithDir());
			}
			else if (dep->type == Type::Static)
			{
				const auto lib = ninjaWriter.Escape(depConfig.getOutputNameWithDir());
				depsTargets += " " + lib;
				depLink     += " " + lib;
			}
			else if (dep->type == Type::App || dep->type == Type::Utility)
			{
				depsTargets += " " + ninjaWriter.Escape(depConfig.getOutputNameWithDir());
			}
		}
		if (type == Type::Utility)
		{
			ss << "\nbuild " << ninjaWriter.Escape(config.getOutputNameWithDir()) << ": phony || " << depsTargets << "\n";

			continue;
		}

		std::string depObjs = "";

		StringVector cmdDefines;
		for (const auto & def : config.defines)
			cmdDefines.push_back("/D" + def);
		StringVector cmdIncludes;
		for (const auto & inc : config.includes)
			cmdIncludes.push_back("/I" + inc);
		const std::string preprocessor =
			   "  DEFINES = " + joinVector(cmdDefines) + "\n"
			   "  INCLUDES = " + joinVector(cmdIncludes) +"\n";
		for (const auto & filename : this->clCompileFiles)
		{
			auto fullObjName = getObjectName(filename, config.intDir);
			depObjs += " " + fullObjName;

			ss << "build " << fullObjName << ": CXX_COMPILER " << ninjaWriter.Escape(filename) << " || " << orderOnlyTarget  << "\n";
			ss << "  FLAGS = " + joinVector(config.flags) + "\n"
				<< preprocessor;
			ss << "  TARGET_COMPILE_PDB = " << config.intDir << targetName << ".pdb\n";
		}
		for (const auto & filename : this->rcCompileFiles)
		{
			auto fullObjName = getObjectName(filename, config.intDir);
			depObjs += " " + fullObjName;

			ss << "build " << fullObjName << ": RC_COMPILER " << ninjaWriter.Escape(filename) << " || " << orderOnlyTarget  << "\n";
			ss << preprocessor;
		}


		std::string linkLibraries = joinVector(config.link);
		//ss << "\nbuild " << libTargetName << ": phony " << depObjs << "\n";
		std::string linkFlags = joinVector(config.linkFlags);
		if (type == Type::App || type == Type::Dynamic)
		{
			const bool useRsp = linkFlags.size() + linkLibraries.size() + depLink.size() + depObjs.size() > 8000; // do not support NT 4
			const std::string ruleSuffix = useRsp ? "_RSP" : "";
			if (type == Type::App)
				ss << "\nbuild " << ninjaWriter.Escape(config.getOutputNameWithDir()) << ": CXX_EXECUTABLE_LINKER" << ruleSuffix << " " << depObjs << " | " << depLink << " || " << depsTargets << "\n";
			else
				ss << "\nbuild " << ninjaWriter.Escape(config.getImportNameWithDir()) << " " << ninjaWriter.Escape(config.getOutputNameWithDir()) << ": CXX_SHARED_LIBRARY_LINKER" << ruleSuffix << " " << depObjs << " | " << depLink << " || " << depsTargets << "\n";

			ss << "  FLAGS = \n"
			"  LINK_FLAGS = " << linkFlags << "\n"
			"  LINK_LIBRARIES = " << linkLibraries << " " << depLink << "\n"
			"  OBJECT_DIR = " << targetName << ".dir\\" << config.name << "\n"
			"  POST_BUILD = cd .\n"
			"  PRE_LINK = cd .\n"
			"  TARGET_COMPILE_PDB = " << targetName << ".dir\\" << config.name << "\\ \n"
			"  TARGET_FILE = " << ninjaWriter.Escape(config.getOutputNameWithDir()) << "\n"
			"  TARGET_IMPLIB = " << ninjaWriter.Escape(config.getImportNameWithDir()) << "\n"
			"  TARGET_PDB = " << ninjaWriter.Escape(config.outDir + config.targetName) << ".pdb\n"
				  ;
			if (useRsp)
				ss << "  RSP_FILE = "<< config.name << "\\" << config.targetName << ".rsp\n";
		}
		else if (type == Type::Static)
		{
			ss << "\nbuild " << ninjaWriter.Escape(config.getOutputNameWithDir()) << ": CXX_STATIC_LIBRARY_LINKER " << depObjs << " || " << depsTargets << "\n"
		   "  LANGUAGE_COMPILE_FLAGS =\n"
		   "  LINK_FLAGS = " << linkFlags << "\n"
		   "  OBJECT_DIR = " << targetName << ".dir\\" << config.name << "\n"
		   "  POST_BUILD = cd .\n"
		   "  PRE_LINK = cd .\n"
		   "  TARGET_COMPILE_PDB = " << targetName << ".dir\\" << config.name << "\\" << config.targetName << ".pdb\n"
		   "  TARGET_FILE = " << ninjaWriter.Escape(config.getOutputNameWithDir()) << "\n"
		   "  TARGET_PDB = " << config.name << "\\" << config.targetName << ".pdb\n"
				  ;
		}
		configIndex++;
	}

	return ss.str();
}


std::ostream &operator <<(std::ostream &os, const VcProjectInfo::Config &info) {
	os << "config (" << info.configuration << "|" << info.platform << "):";
	os << info.clVariables;
	os << "\n";
	return os;
}

std::ostream &operator <<(std::ostream &os, const VcProjectInfo::ParsedConfig &info) {
	os << "PREPARED (" << info.name << "): \n";
	os << "\t\tINCLUDES=" << info.includes << "\n";
	os << "\t\tDEFINES=" << info.defines << "\n";
	os << "\t\tFLAGS=" << info.flags << "\n";
	os << "\t\tLINK=" << info.link << "\n";
	os << "\t\tLINKFLAGS=" << info.linkFlags << "\n";
	return os;
}

std::ostream &operator <<(std::ostream &os, const VcProjectInfo &info) {
	os << "vcproj (" << info.targetName << "=" << info.fileName << "):" << info.GUID;
	for (const auto & dep : info.dependentGuids)
		os << "\n\tdep <- " << dep;
	for (const auto & clFile : info.clCompileFiles)
		os << "\n\tCL: " << clFile;
	os << "\n";
	for (const auto & cfg : info.configs)
		os << "\t" << cfg;
	for (const auto & cfg : info.parsedConfigs)
		os << "\t" << cfg;
	os << "\n";
	return os;
}
