#pragma once

#include "CommonTypes.h"

#include <map>
#include <sstream>
#include <set>

struct VcProjectInfo;

class NinjaWriter
{
	std::map<std::string, std::string> idents;
	std::ostringstream identsDecl;
	std::ostringstream targetRules;
	std::set<std::string> existingRules;
	int maxIdent = 0;
	const std::string buildRoot, cmakeExe;
public:
	NinjaWriter(const std::string & buildRoot_, const std::string & cmakeExe_) : buildRoot(buildRoot_), cmakeExe(cmakeExe_) {}
	std::string Escape(std::string value);
	std::string Escape(const StringVector & values);

	void WriteFile(bool verbose) const;

	void GenerateNinjaRules(const VcProjectInfo & project);
};
