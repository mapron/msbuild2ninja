#include <iostream>
#include <string>
#include <filesystem>
#include <regex>
#include <sstream>
#include <fstream>
#include <chrono>

#include <filesystem>
namespace fs = std::filesystem;
using fserr  = std::error_code;

#include "FileUtils.h"
#include "VcProjectInfo.h"
#include "CommandLine.h"

void parseSln(const std::string& slnBase, const std::string& slnName, VcProjectList& vcprojs, const bool dryRun)
{
    std::string filestr;
    if (!FileInfo(slnBase + "/" + slnName).ReadFile(filestr))
        throw std::runtime_error("Failed to read .sln file");

    std::smatch res;

    std::regex                  exp(R"rx(Project\("\{[0-9A-F-]+\}"\) = "(\w+)", "(\w+\.vcxproj)", "\{([0-9A-F-]+)\}"\s*ProjectSection\(ProjectDependencies\) = postProject)rx", std::regex_constants::ECMAScript | std::regex_constants::optimize);
    std::regex                  exp2(R"rx(\{([0-9A-F-]+)\} = )rx", std::regex_constants::ECMAScript | std::regex_constants::optimize);
    std::string::const_iterator searchStart(filestr.cbegin());
    std::string                 ALL_BUILD_GUID;
    while (std::regex_search(searchStart, filestr.cend(), res, exp)) {
        VcProjectInfo info;
        info.baseDir    = slnBase;
        info.targetName = res[1].str();
        info.fileName   = res[2].str();
        info.GUID       = res[3].str();

        if (info.targetName == "ALL_BUILD")
            ALL_BUILD_GUID = info.GUID;

        size_t      posStart = searchStart - filestr.cbegin() + res.position() + res.length();
        size_t      posEnd   = filestr.find("EndProjectSection", posStart);
        std::smatch res2;

        std::string::const_iterator searchStart2(filestr.cbegin() + posStart);
        while (std::regex_search(searchStart2, filestr.cbegin() + posEnd, res2, exp2)) {
            info.dependentGuids.push_back(res2[1].str());
            searchStart2 += res2.position() + res2.length();
        }
        vcprojs.push_back(info);
        searchStart += res.position() + res.length();
    }
    std::regex post("postProject[\r\n\t {}=0-9A-F-]+EndProjectSection", std::regex_constants::ECMAScript | std::regex_constants::optimize);
    filestr = std::regex_replace(filestr, post, "postProject\n\tEndProjectSection");
#if 0 // todo:
	std::regex buildGuid(R"rx(\{([0-9A-F-]+)\}\.\w+\|\w+.Build\.0 = \w+\|\w+)rx", std::regex_constants::ECMAScript | std::regex_constants::optimize);
	searchStart = filestr.cbegin();
	while ( std::regex_search( searchStart, filestr.cend(), res, buildGuid ) )
	{
		const std::string GUID       = res[1].str();
		if (!ALL_BUILD_GUID.empty() && GUID != ALL_BUILD_GUID)
		{
			filestr.erase(searchStart + res.position(), searchStart + res.position() + res.length() );
			continue;
		}

		searchStart += res.position() + res.length();
	}
#endif
    if (!dryRun && !FileInfo(slnBase + "/" + slnName).WriteFile(filestr))
        throw std::runtime_error("Failed to write file:" + slnName);
}

int main(int argc, char* argv[])
{
    try {
        CommandLine cmd(argc, argv);
        auto        checkFile = fs::path(cmd.rootDir) / (cmd.slnFile + ".timestamp");
        if (!cmd.dryRun) {
            std::error_code ec;
            const auto      slnTime    = fs::last_write_time(fs::path(cmd.rootDir) / cmd.slnFile, ec);
            const auto      checkTime  = fs::last_write_time(checkFile, ec);
            const bool      needUpdate = slnTime > checkTime;
            if (!needUpdate) {
                std::cout << "Solution is up-to-date, skipping" << std::endl;
                return 0;
            }
        }

        //auto start = std::chrono::system_clock::now();
        VcProjectList vcprojs;
        parseSln(cmd.rootDir, cmd.slnFile, vcprojs, cmd.dryRun);

        for (auto& p : vcprojs) {
            p.ReadVcProj();
            p.ParseConfigs();
            p.TransformConfigs(cmd.configs, cmd.rootDir);
            p.ConvertToMakefile(cmd.ninjaExe, cmd.additionalDeps[p.targetName]);
            if (!cmd.dryRun)
                p.WriteVcProj();
        }

        NinjaWriter ninjaWriter(cmd.rootDir, cmd.cmakeExe);
        for (auto& p : vcprojs) {
            p.CalculateDependentTargets(vcprojs);
            ninjaWriter.GenerateNinjaRules(p);
        }
        //		std::cout << "Parsed projects:\n";
        //		for (const auto & p : vcprojs)
        //			std::cout << p;

        ninjaWriter.WriteFile(cmd.verbose);
        if (!cmd.dryRun) {
            FileInfo(checkFile.u8string()).WriteFile("1");
        }
        for (const auto& config : cmd.configs) {
            FileInfo(cmd.rootDir + "/" + config + "/").Mkdirs();
        }
        //std::cout <<  "Elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now() - start ).count() << '\n';
    }
    catch (std::exception& e) {
        std::cout << e.what() << std::endl;
        return 1;
    }
    return 0;
}
