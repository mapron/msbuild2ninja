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


std::ostream & operator << (std::ostream & os, const StringVector & lst) {
	for (const auto & el : lst)
		os << el << " ";
	return os;
}

std::ostream & operator << (std::ostream & os, const VcProjectInfo::Config & info) {
	os << "config (" << info.configuration << "|" << info.platform << "):";
	os << info.clVariables;
	os << "\n";
	return os;
}

std::ostream & operator << (std::ostream & os, const VcProjectInfo::ParsedConfig & info) {
	os << "PREPARED (" << info.name << "): \n";
	os << "\t\tINCLUDES=" << info.includes << "\n";
	os << "\t\tDEFINES=" << info.defines << "\n";
	os << "\t\tFLAGS=" << info.flags << "\n";
	os << "\t\tLINK=" << info.link << "\n";
	os << "\t\tLINKFLAGS=" << info.linkFlags << "\n";
	return os;
}

std::ostream & operator << (std::ostream & os, const VcProjectInfo & info) {
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
	//	std::cout << "read tree: " << info.targetName << std::endl;
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

		std::ostringstream ninjaBuildContents;
		ninjaBuildContents << "ninja_required_version = 1.5\n";
		ninjaBuildContents << "msvc_deps_prefix = Note: including file: \n";
		ninjaBuildContents << "rule CXX_COMPILER\n"
							"  deps = msvc\n"
							"  command = cl.exe  /nologo $DEFINES $INCLUDES $FLAGS /showIncludes /Fo$out /Fd$TARGET_COMPILE_PDB /FS -c $in\n"
							"  description = Building CXX object $out\n\n";

		ninjaBuildContents << "rule CXX_STATIC_LIBRARY_LINKER\n"
						   "  command = cmd.exe /C \"$PRE_LINK && link.exe /lib /nologo $LINK_FLAGS /out:$TARGET_FILE $in  && $POST_BUILD\"\n"
						   "  description = Linking CXX static library $TARGET_FILE\n"
						   "  restat = $RESTAT\n";

		ninjaBuildContents << "rule CXX_SHARED_LIBRARY_LINKER\n"
			"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_dll --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo $in  /out:$TARGET_FILE /implib:$TARGET_IMPLIB /pdb:$TARGET_PDB /dll /version:0.0 $LINK_FLAGS $LINK_PATH $LINK_LIBRARIES  && $POST_BUILD\"\n"
			"  description = Linking CXX shared library $TARGET_FILE\n"
			"  restat = 1\n";

		ninjaBuildContents << "rule CXX_SHARED_LIBRARY_LINKER_RSP\n"
			"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_dll --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo @$RSP_FILE  /out:$TARGET_FILE /implib:$TARGET_IMPLIB /pdb:$TARGET_PDB /dll /version:0.0 $LINK_FLAGS  && $POST_BUILD\"\n"
			"  description = Linking CXX shared library $TARGET_FILE\n"
			"  rspfile = $RSP_FILE\n"
			"  rspfile_content = $in_newline $LINK_PATH $LINK_LIBRARIES \n"
			"  restat = 1\n";

		ninjaBuildContents << "rule CXX_EXECUTABLE_LINKER\n"
			"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_exe --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo $in  /out:$TARGET_FILE /pdb:$TARGET_PDB /version:0.0  $LINK_FLAGS $LINK_PATH $LINK_LIBRARIES && $POST_BUILD\"\n"
			"  description = Linking CXX executable $TARGET_FILE\n"
			"  restat = $RESTAT\n";

		ninjaBuildContents << "rule CXX_EXECUTABLE_LINKER_RSP\n"
			"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_exe --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo @$RSP_FILE  /out:$TARGET_FILE /pdb:$TARGET_PDB /version:0.0  $LINK_FLAGS && $POST_BUILD\"\n"
			"  rspfile = $RSP_FILE\n"
			"  rspfile_content = $in_newline $LINK_PATH $LINK_LIBRARIES \n"
			"  description = Linking CXX executable $TARGET_FILE\n"
			"  restat = $RESTAT\n";

		ninjaBuildContents << "rule CUSTOM_COMMAND\n"
							  "  command = $COMMAND\n"
							  "  description = $DESC\n";

		ninjaBuildContents << "rule RC_COMPILER\n"
							 "  command = rc.exe $DEFINES $INCLUDES $FLAGS /fo$out $in\n"
							 "  description = Building RC object $out\n";

		for (auto & p : vcprojs)
		{
			p.ParseConfigs();
			p.TransformConfigs({"Debug", "Release"}, rootDir);
			p.ConvertToMakefile(ninjaExe, dryRun);
		}
		std::set<std::string> existingRules;
		for (auto & p : vcprojs)
		{
			p.CalculateDependentTargets(vcprojs);
			ninjaBuildContents << p.GetNinjaRules(rootDir, existingRules);
		}
		//std::cout << "Parsed projects:\n";
		//for (const auto & p : vcprojs)
		//	std::cout << p;

		std::string buildNinja = ninjaBuildContents.str();
		std::cout << "\nNinja file:\n" << buildNinja;

		FileInfo(rootDir + "/build.ninja").WriteFile(buildNinja, false);

	} catch(std::exception & e) {
		std::cout << e.what() << std::endl;
		return 1;
	}
	return 0;
}
