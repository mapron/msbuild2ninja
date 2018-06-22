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
	if (!FileInfo(baseDir + "/" + fileName + ".filters").ReadFile(projectFiltersData))
		throw std::runtime_error("Failed to read project file");
}

void VcProjectInfo::WriteVcProj()
{
	if (!FileInfo(baseDir + "/" + fileName).WriteFile(projectFileData))
		 throw std::runtime_error("Failed to write file:" + fileName);
	if (!FileInfo(baseDir + "/" + fileName + ".filters").WriteFile(projectFiltersData))
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
		static const std::regex exp(R"rx(<ConfigurationType>(\w+)</ConfigurationType>)rx", std::regex_constants::ECMAScript | std::regex_constants::optimize);

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
		static const std::regex exp2(R"rx(<(\w+) Condition="'\$\(Configuration\)\|\$\(Platform\)'=='(\w+)\|(\w+)'">([^<]+)</)rx", std::regex_constants::ECMAScript | std::regex_constants::optimize);

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
	static const std::regex exp3(R"rx(<ItemDefinitionGroup Condition="'\$\(Configuration\)\|\$\(Platform\)'=='(\w+)\|(\w+)'">\s*)rx", std::regex_constants::ECMAScript | std::regex_constants::optimize);

	std::string::const_iterator searchStart( projectFileData.cbegin() );
	while ( std::regex_search( searchStart, projectFileData.cend(), res, exp3 ) )
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
	for (const auto & configName : configurations)
	{
		auto configIt = std::find_if( configs.begin(), configs.end(), [&configName](const Config & config){ return config.configuration == configName;});
		if (configIt == configs.end())
			continue;
		const Config & config = *configIt;

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
		std::replace(pc.outDir.begin(), pc.outDir.end(), '/', '\\');

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
		flagsProcess("RuntimeLibrary", {{"MultiThreadedDLL", "/MD"}, {"MultiThreadedDebugDLL", "/MDd"},{"MultiThreaded", "/MT"}, {"MultiThreadedDebug", "/MTd"}});
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

	static const std::regex re("<ConfigurationType>\\w+</ConfigurationType>", std::regex_constants::ECMAScript | std::regex_constants::optimize);

	projectFileData.erase(projectFileData.begin() + posRef, projectFileData.begin() + posRefEnd + refEnd.size());

	projectFileData = std::regex_replace(projectFileData,
										 re,
										 "<ConfigurationType>Makefile</ConfigurationType>");

	static std::regex customBuild("<CustomBuild Include=\"([^\"]+)\">\\s*<Filter>([^<]+)</Filter>\\s*</CustomBuild>", std::regex_constants::ECMAScript | std::regex_constants::optimize);

	{
		std::smatch res;
		std::string::const_iterator searchStart( projectFiltersData.cbegin() );
		std::string replacedText;
		while ( std::regex_search( projectFiltersData.cbegin(), projectFiltersData.cend(), res, customBuild ) )
		{
			auto filename = res[1].str();
			const bool ignore = filename.find(".rule") != std::string::npos;
			const std::string replaceStr = ignore ? "" : "<None Include=\"" + filename + "\"><Filter>" + res[2].str() + "</Filter></None>";
			projectFiltersData.replace(projectFiltersData.begin() + res.position(), projectFiltersData.begin() + res.position() + res.length(), replaceStr);
			if (!ignore)
			{
				replacedText += replaceStr + "\n";
			}
		}
		auto lastGroup = projectFileData.rfind("</ItemGroup>");
		if (!replacedText.empty())
			projectFileData.insert(lastGroup + 12, "<ItemGroup>\n" + replacedText + "</ItemGroup>\n");
	}

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
		static const std::regex re("(if |cd |exit |setlocal|endlocal|:).*[\r]?", std::regex_constants::ECMAScript | std::regex_constants::optimize);
		static const std::regex reNL("[\r\n]", std::regex_constants::ECMAScript | std::regex_constants::optimize);
		auto lines = strToList(data, '\n');
		std::string result = "";
		for (const auto & line : lines)
		{
			if (line.size() < 4)
				continue;
			if (std::regex_match(line, re))
				continue;

			if (!result.empty()) result += " && ";
			result += std::regex_replace(line, reNL, "");
		}
		return result;
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
			if (rule.output.find(";") != std::string::npos)
			{
				auto outputs = strToList(rule.output);
				rule.output = outputs[0];
				outputs.erase(outputs.begin());
				rule.additionalOutputs = outputs;
			}
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
			dependentTargets.push_back(&*it);
		}
	}
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
