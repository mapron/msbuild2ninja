#include "NinjaWriter.h"
#include "FileUtils.h"
#include "VcProjectInfo.h"

#include <regex>
#include <iostream>
#include <cassert>

std::string NinjaWriter::Escape(std::string value)
{
	if (value.find(buildRoot) == 0)
		value = value.substr( buildRoot.size() + 1);

	if (value.find('(') != std::string::npos || value.find(')') != std::string::npos)
	{
		auto it = idents.find(value);
		if (it != idents.cend())
			return "$" + it->second;
		const std::string newIdent = "ident" + std::to_string(maxIdent++);
		identsDecl << newIdent << " = " << value << "\n";
		idents[value] = newIdent;
		return "$" + newIdent;
	}
	static const std::regex re("([ ]|[:])", std::regex_constants::ECMAScript | std::regex_constants::optimize);
	return std::regex_replace(value, re, "$$$1"); // "$$" converts to "$" symbol, and $1 captures 1 match.
}

std::string NinjaWriter::Escape(const StringVector &values)
{
	std::string result;
	for (const auto & value : values)
	{
		result += " " + Escape(value);
	}
	return result;
}

void NinjaWriter::WriteFile(bool verbose) const
{
	std::ostringstream ninjaHeader;
	ninjaHeader << "ninja_required_version = 1.5\n";
	ninjaHeader << "msvc_deps_prefix = Note: including file: \n";
	ninjaHeader << "rule CXX_COMPILER\n"
						"  deps = msvc\n"
						"  command = cl.exe  /nologo $DEFINES $INCLUDES $FLAGS /showIncludes /Fo$out /Fd$TARGET_COMPILE_PDB /FS -c $in\n"
						"  description = Building CXX object $out\n\n";

	ninjaHeader << "rule CXX_STATIC_LIBRARY_LINKER\n"
					   "  command = cmd.exe /C \"$PRE_LINK && link.exe /lib /nologo $LINK_FLAGS /out:$TARGET_FILE $in  && $POST_BUILD\"\n"
					   "  description = Linking CXX static library $TARGET_FILE\n"
					   "  restat = $RESTAT\n";

	ninjaHeader << "rule CXX_SHARED_LIBRARY_LINKER\n"
		"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_dll --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo $in  /out:$TARGET_FILE /implib:$TARGET_IMPLIB /pdb:$TARGET_PDB /dll /version:0.0 $LINK_FLAGS $LINK_PATH $LINK_LIBRARIES  && $POST_BUILD\"\n"
		"  description = Linking CXX shared library $TARGET_FILE\n"
		"  restat = 1\n";

	ninjaHeader << "rule CXX_SHARED_LIBRARY_LINKER_RSP\n"
		"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_dll --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo @$RSP_FILE  /out:$TARGET_FILE /implib:$TARGET_IMPLIB /pdb:$TARGET_PDB /dll /version:0.0 $LINK_FLAGS  && $POST_BUILD\"\n"
		"  description = Linking CXX shared library $TARGET_FILE\n"
		"  rspfile = $RSP_FILE\n"
		"  rspfile_content = $in_newline $LINK_PATH $LINK_LIBRARIES \n"
		"  restat = 1\n";

	ninjaHeader << "rule CXX_EXECUTABLE_LINKER\n"
		"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_exe --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo $in  /out:$TARGET_FILE /pdb:$TARGET_PDB /version:0.0  $LINK_FLAGS $LINK_PATH $LINK_LIBRARIES && $POST_BUILD\"\n"
		"  description = Linking CXX executable $TARGET_FILE\n"
		"  restat = $RESTAT\n";

	ninjaHeader << "rule CXX_EXECUTABLE_LINKER_RSP\n"
		"  command = cmd.exe /C \"$PRE_LINK && \"" << cmakeExe << "\" -E vs_link_exe --intdir=$OBJECT_DIR --manifests $MANIFESTS -- link.exe /nologo @$RSP_FILE  /out:$TARGET_FILE /pdb:$TARGET_PDB /version:0.0  $LINK_FLAGS && $POST_BUILD\"\n"
		"  rspfile = $RSP_FILE\n"
		"  rspfile_content = $in_newline $LINK_PATH $LINK_LIBRARIES \n"
		"  description = Linking CXX executable $TARGET_FILE\n"
		"  restat = $RESTAT\n";

	ninjaHeader << "rule CUSTOM_COMMAND\n"
						  "  command = $COMMAND\n"
						  "  description = $DESC\n";

	ninjaHeader << "rule RC_COMPILER\n"
						 "  command = rc.exe $DEFINES $INCLUDES $FLAGS /fo$out $in\n"
						 "  description = Building RC object $out\n";



	const std::string buildNinja = ninjaHeader.str() + identsDecl.str() + targetRules.str();
	if (verbose)
		std::cout << "\nNinja file:\n" << buildNinja;

	FileInfo(buildRoot + "/build.ninja").WriteFile(buildNinja, false);
}

void NinjaWriter::GenerateNinjaRules(const VcProjectInfo &project)
{
	std::ostringstream ss;
	using Type = VcProjectInfo::Type;
	const auto & type = project.type;
	if (type == Type::Unknown)
		return;

	std::set<std::string> existingObjNames;
	auto getObjectName = [&existingObjNames](const std::string & filename, const std::string & intDir)
	{
		static const std::regex re("\\.(cpp|rc)$", std::regex_constants::ECMAScript | std::regex_constants::optimize);
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

	for (const auto & config : project.parsedConfigs)
	{
		std::string orderOnlyTarget;
		std::string orderDeps;
		for (const auto & customCmd : config.customCommands)
		{
			const auto escapedOut = this->Escape(customCmd.output);
			if (existingRules.find(escapedOut) == existingRules.end())
			{
				if (customCmd.command.empty())
					ss << "\nbuild " << escapedOut << " " << this->Escape(customCmd.additionalOutputs) << ": phony " << this->Escape(customCmd.deps) <<  "\n";
				else
					ss << "\nbuild " << escapedOut << " " << this->Escape(customCmd.additionalOutputs) << ": CUSTOM_COMMAND " << this->Escape(customCmd.deps) <<  "\n"
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
		for (const VcProjectInfo * dep : project.dependentTargets)
		{
			const auto & depConfig = *std::find_if( dep->parsedConfigs.cbegin(), dep->parsedConfigs.cend(), [&config](const VcProjectInfo::ParsedConfig & pc) {
				return pc.name == config.name;
			});
			if (dep->type == Type::Dynamic)
			{
				depLink     += " " + this->Escape(depConfig.getImportNameWithDir());
				depsTargets += " " + this->Escape(depConfig.getOutputNameWithDir());
			}
			else if (dep->type == Type::Static)
			{
				const auto lib = this->Escape(depConfig.getOutputNameWithDir());
				depsTargets += " " + lib;
				depLink     += " " + lib;
			}
			else if (dep->type == Type::App || dep->type == Type::Utility)
			{
				depsTargets += " " + this->Escape(depConfig.getOutputNameWithDir());
			}
		}
		if (type == Type::Utility)
		{
			ss << "\nbuild " << this->Escape(config.getOutputNameWithDir()) << ": phony || " << depsTargets << "\n";

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
		for (const auto & filename : project.clCompileFiles)
		{
			auto fullObjName = getObjectName(filename, config.intDir);
			depObjs += " " + fullObjName;

			ss << "build " << fullObjName << ": CXX_COMPILER " << this->Escape(filename) << " || " << orderOnlyTarget  << "\n";
			ss << "  FLAGS = " + joinVector(config.flags) + "\n"
				<< preprocessor;
			ss << "  TARGET_COMPILE_PDB = " << config.intDir << project.targetName << ".pdb\n";
		}
		for (const auto & filename : project.rcCompileFiles)
		{
			auto fullObjName = getObjectName(filename, config.intDir);
			depObjs += " " + fullObjName;

			ss << "build " << fullObjName << ": RC_COMPILER " << this->Escape(filename) << " || " << orderOnlyTarget  << "\n";
			ss << preprocessor;
		}


		std::string linkLibraries = joinVector(config.link);
		//ss << "\nbuild " << libTargetName << ": phony " << depObjs << "\n";
		std::string linkFlags = joinVector(config.linkFlags);
		if (type == Type::App || type == Type::Dynamic)
		{
			const bool useRsp = linkFlags.size() + linkLibraries.size() + depLink.size() + depObjs.size() > 2000; // do not support NT 4
			const std::string ruleSuffix = useRsp ? "_RSP" : "";
			if (type == Type::App)
				ss << "\nbuild " << this->Escape(config.getOutputNameWithDir()) << ": CXX_EXECUTABLE_LINKER" << ruleSuffix << " " << depObjs << " | " << depLink << " || " << depsTargets << "\n";
			else
				ss << "\nbuild " << this->Escape(config.getImportNameWithDir()) << " " << this->Escape(config.getOutputNameWithDir()) << ": CXX_SHARED_LIBRARY_LINKER" << ruleSuffix << " " << depObjs << " | " << depLink << " || " << depsTargets << "\n";

			ss << "  FLAGS = \n"
			"  LINK_FLAGS = " << linkFlags << "\n"
			"  LINK_LIBRARIES = " << linkLibraries << " " << depLink << "\n"
			"  OBJECT_DIR = " << project.targetName << ".dir\\" << config.name << "\n"
			"  POST_BUILD = cd .\n"
			"  PRE_LINK = cd .\n"
			"  TARGET_COMPILE_PDB = " << project.targetName << ".dir\\" << config.name << "\\ \n"
			"  TARGET_FILE = " << this->Escape(config.getOutputNameWithDir()) << "\n"
			"  TARGET_IMPLIB = " << this->Escape(config.getImportNameWithDir()) << "\n"
			"  TARGET_PDB = " << this->Escape(config.outDir + config.targetName) << ".pdb\n"
				  ;
			if (useRsp)
				ss << "  RSP_FILE = "<< config.name << "\\" << config.targetName << ".rsp\n";
		}
		else if (type == Type::Static)
		{
			ss << "\nbuild " << this->Escape(config.getOutputNameWithDir()) << ": CXX_STATIC_LIBRARY_LINKER " << depObjs << " || " << depsTargets << "\n"
		   "  LANGUAGE_COMPILE_FLAGS =\n"
		   "  LINK_FLAGS = " << linkFlags << "\n"
		   "  OBJECT_DIR = " << project.targetName << ".dir\\" << config.name << "\n"
		   "  POST_BUILD = cd .\n"
		   "  PRE_LINK = cd .\n"
		   "  TARGET_COMPILE_PDB = " << project.targetName << ".dir\\" << config.name << "\\" << config.targetName << ".pdb\n"
		   "  TARGET_FILE = " << this->Escape(config.getOutputNameWithDir()) << "\n"
		   "  TARGET_PDB = " << config.name << "\\" << config.targetName << ".pdb\n"
				  ;
		}
	}

	targetRules << ss.str();
}
