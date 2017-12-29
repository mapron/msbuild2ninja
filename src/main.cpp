#include <iostream>
#include <string>
#include <filesystem>
#include <regex>
#include <sstream>
#include <fstream>
/*
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/utime.h>
#include <time.h>
*/
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
	for (const auto & dep : info.variables)
		os << "\n\t\t" << dep.first << "=" << dep.second;
	os << "\n";
	return os;
}

std::ostream & operator << (std::ostream & os, const VcProjectInfo::ParsedConfig & info) {
	os << "PREPARED (" << info.name << "): \n";
	os << "\t\tINCLUDES=" << info.includes << "\n";
	os << "\t\tDEFINES=" << info.defines << "\n";
	os << "\t\tFLAGS=" << info.flags << "\n";
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

void parseSln(const std::string & slnBase, const std::string & slnName, VcProjectList & vcprojs)
{
	ByteArrayHolder data;
	if (!FileInfo(slnBase + "/" + slnName).ReadFile(data))
		throw std::runtime_error("Failed to read .sln file");

	std::string filestr((const char*)data.data(), data.size());
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
		//std::string deps = res[4].str();
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
}
/*
struct StateDir
{
	std::map<std::string, int64_t> files;
	void check(const std::string & rootDir)
	{
		fs::recursive_directory_iterator it(rootDir);
		for (; it != fs::recursive_directory_iterator(); ++it)
		{
			if (fs::is_regular_file( it->path() ))
			{
				auto p = it->path().u8string();
				auto t = fs::last_write_time( it->path() ).time_since_epoch().count();
				files[p] = t;
				//std::cout << p << " = " << t << "\n";
			}
		}
	}
	void save(const std::string & filename)
	{
		std::ofstream fout;
		fout.open(filename, std::ios::out);
		if (!fout)
			return;
		fout << files.size() << "\n";
		for (auto p : files)
			fout << p.first << "\n" << p.second << "\n";
	}
	void load(const std::string & filename)
	{
		std::ifstream fin;
		fin.open(filename, std::ios::in);
		if (!fin)
			return;
		size_t size;
		fin >> size;
		for (size_t i =0; i< size; ++i)
		{
			std::string fname;
			int64_t mtime;
			fin >> fname >> mtime;
			files[fname] = mtime;
		}
	}
	void outputDiff(const StateDir & prev)
	{
		if (prev.files == files)
			std::cout << "Nothing changed.\n";
		for (auto p : prev.files)
		{
			auto newtime = files[p.first];
			if (newtime != p.second)
			{
				std::cout << p.first << ": " << p.second << " -> " << newtime << "\n";
			}
		}
	}
};*/

/*
void SetUtimeAsFile(std::vector<std::string> files, std::string file)
{
	struct stat s;

	stat(file.c_str(), &s);
	int64_t inT =  fs::last_write_time( file ).time_since_epoch().count();
	std::cout << file << " has " << inT << "\n\n";
	struct _utimbuf t;
	t.actime = s.st_atime + 1;
	t.modtime = s.st_mtime + 1;
	for (auto f : files)
	{
		std::replace(f.begin(), f.end(), '/', '\\');
		int r = _utime(f.c_str(), &t);
		int64_t outT =  fs::last_write_time( f ).time_since_epoch().count();
		std::cout << f << " now has " << outT << "\n";
	}
}*/

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		std::cout << "usage: <msbuild directory> <cl.exe>\n";
		return 1;
	}
	try {
		const std::string rootDir = argv[1];
	/*	SetUtimeAsFile( {
	"E:/testGen/build64/Release/hello_s.lib",
"E:/testGen/build64/hello_s.dir/Release/hello_s.log",
"E:/testGen/build64/hello_s.dir/Release/hello_s.tlog/CL.command.1.tlog",
"E:/testGen/build64/hello_s.dir/Release/hello_s.tlog/CL.read.1.tlog",
"E:/testGen/build64/hello_s.dir/Release/hello_s.tlog/CL.write.1.tlog",
"E:/testGen/build64/hello_s.dir/Release/hello_s.tlog/Lib-link.read.1.tlog",
"E:/testGen/build64/hello_s.dir/Release/hello_s.tlog/Lib-link.write.1.tlog",
"E:/testGen/build64/hello_s.dir/Release/hello_s.tlog/hello_s.lastbuildstate",
"E:/testGen/build64/hello_s.dir/Release/hello_s.tlog/lib.command.1.tlog",
						}, "E:/testGen/build64/hello_s.dir/Release/lib1.obj");
		//return 0;
*/
		/*
		StateDir prev;
		prev.load("prev_state");
		StateDir cur;
		cur.check(rootDir);
		cur.outputDiff(prev);
		cur.save("prev_state");
		return 0;*/

		const std::string clexe = argv[2];
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
		parseSln(rootDir,  slnFiles[0], vcprojs);

		std::ostringstream ninjaBuildContents;
		ninjaBuildContents << "ninja_required_version = 1.5\n";
		ninjaBuildContents << "msvc_deps_prefix = Note: including file: \n";
		ninjaBuildContents << "rule CXX_COMPILER\n"
							"  deps = msvc\n"
							"  command = " << clexe << "  /nologo /TP $DEFINES $INCLUDES $FLAGS /showIncludes /Fo$out /Fd$TARGET_COMPILE_PDB /FS -c $in\n"
							"  description = Building CXX object $out\n\n";

		for (auto & p : vcprojs)
		{
			p.ParseFilters();
			p.ParseConfigs();
			p.TransformConfigs({"Debug", "Release"});
		}
		for (auto & p : vcprojs)
		{
			p.CalculateDependentTargets(vcprojs);
			ninjaBuildContents << p.GetNinjaRules();
		}
		std::cout << "Parsed projects:\n";
		for (const auto & p : vcprojs)
			std::cout << p;

		std::string buildNinja = ninjaBuildContents.str();
		std::cout << "\nNinja file:\n" << buildNinja;

		ByteArrayHolder data;
		data.resize(buildNinja.size());
		memcpy(data.data(), buildNinja.c_str(), buildNinja.size());
		FileInfo(rootDir + "/build.ninja").WriteFile(data, false);

	} catch(std::exception & e) {
		std::cout << e.what() << std::endl;
		return 1;
	}
	return 0;
}
