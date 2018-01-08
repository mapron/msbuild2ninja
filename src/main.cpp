#include <iostream>
#include <string>
#include <filesystem>
#include <regex>
#include <sstream>
#include <fstream>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
using fserr = std::error_code;

#include "FileUtils.h"
#include "VcProjectInfo.h"
#include "CommandLine.h"


void parseSln(const std::string & slnBase, const std::string & slnName, VcProjectList & vcprojs, const bool dryRun)
{
	std::string filestr;
	if (!FileInfo(slnBase + "/" + slnName).ReadFile(filestr))
		throw std::runtime_error("Failed to read .sln file");

	std::smatch res;
	std::regex exp(R"rx(Project\("\{[0-9A-F-]+\}"\) = "(\w+)", "(\w+\.vcxproj)", "\{([0-9A-F-]+)\}"\s*ProjectSection\(ProjectDependencies\) = postProject)rx");
	std::regex exp2(R"rx(\{([0-9A-F-]+)\} = )rx");
	std::string::const_iterator searchStart( filestr.cbegin() );
	while ( std::regex_search( searchStart, filestr.cend(), res, exp ) )
	{
		VcProjectInfo info;
		info.baseDir    = slnBase;
		info.targetName = res[1].str();
		info.fileName   = res[2].str();
		info.GUID       = res[3].str();

		size_t posStart = searchStart - filestr.cbegin() + res.position() + res.length();
		size_t posEnd   = filestr.find("EndProjectSection", posStart);
		std::smatch res2;

		std::string::const_iterator searchStart2( filestr.cbegin() + posStart );
		while ( std::regex_search( searchStart2, filestr.cbegin() + posEnd, res2, exp2 ) )
		{
			info.dependentGuids.push_back(res2[1].str());
			searchStart2 += res2.position() + res2.length();
		}
		vcprojs.push_back(info);
		searchStart += res.position() + res.length();
	}
	filestr = std::regex_replace(filestr,
										 std::regex("postProject[\r\n\t {}=0-9A-F-]+EndProjectSection"),
										 "postProject\n\tEndProjectSection");

	if (!dryRun && !FileInfo(slnBase + "/" + slnName).WriteFile(filestr))
		 throw std::runtime_error("Failed to write file:" + slnName);
}

int main(int argc, char* argv[])
{
	try {
		CommandLine cmd(argc, argv);

		VcProjectList vcprojs;
		parseSln(cmd.rootDir,  cmd.slnFile, vcprojs, cmd.dryRun);

		for (auto & p : vcprojs)
		{
			p.ReadVcProj();
			p.ParseConfigs();
			p.TransformConfigs(cmd.configs, cmd.rootDir);
			p.ConvertToMakefile(cmd.ninjaExe, cmd.additionalDeps[p.targetName]);
			if (!cmd.dryRun)
				p.WriteVcProj();
		}

		NinjaWriter ninjaWriter(cmd.rootDir, cmd.cmakeExe);
		for (auto & p : vcprojs)
		{
			p.CalculateDependentTargets(vcprojs);
			ninjaWriter.GenerateNinjaRules(p);
		}
		//std::cout << "Parsed projects:\n";
		//for (const auto & p : vcprojs)
		//	std::cout << p;

		ninjaWriter.WriteFile(cmd.verbose);

	} catch(std::exception & e) {
		std::cout << e.what() << std::endl;
		return 1;
	}
	return 0;
}
