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
	if (argc < 4)
	{
		std::cout << "usage: <msbuild directory> <ninja binary> <cmake binary> [dry] \n";
		return 1;
	}
	try {
		const std::string rootDir = argv[1];
		const std::string ninjaExe = argv[2];
		const std::string cmakeExe = argv[3];
		const bool dryRun = argc >= 5 && argv[4] == std::string("dry");
		if (dryRun)
			std::cout << "Dry run.\n";

		StringVector slnFiles;
		for(const fs::directory_entry& it : fs::directory_iterator(rootDir))
		{
			 const fs::path& p = it.path();
			 if (fs::is_regular_file(p) && p.extension() == ".sln")
				slnFiles.push_back( p.filename().u8string() );
		}
		if (slnFiles.size() != 1)
			throw std::invalid_argument("directory should contain exactly one sln file");

		VcProjectList vcprojs;
		parseSln(rootDir,  slnFiles[0], vcprojs, dryRun);

		for (auto & p : vcprojs)
		{
			StringVector customDeps;
			if (p.targetName =="InstallProduct")
				customDeps = StringVector{"Release_InstallBuildFixupRelease"}; // @todo: @fixme:

			p.ReadVcProj();
			p.ParseConfigs();
			p.TransformConfigs({"Release", "Debug"}, rootDir); // order is crucial - Release rules more prioritized on conflict.
			p.ConvertToMakefile(ninjaExe, customDeps);
			if (!dryRun)
				p.WriteVcProj();
		}
		std::set<std::string> existingRules;
		NinjaWriter ninjaWriter(rootDir, cmakeExe);
		std::ostringstream targetsRules;
		for (auto & p : vcprojs)
		{
			p.CalculateDependentTargets(vcprojs);
			targetsRules << p.GetNinjaRules(existingRules, ninjaWriter);
		}
		//std::cout << "Parsed projects:\n";
		//for (const auto & p : vcprojs)
		//	std::cout << p;

		ninjaWriter.WriteFile(targetsRules.str(), /*verbose=*/true);

	} catch(std::exception & e) {
		std::cout << e.what() << std::endl;
		return 1;
	}
	return 0;
}
