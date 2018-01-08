#include "NinjaWriter.h"
#include "FileUtils.h"

#include <regex>
#include <iostream>

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
	return std::regex_replace(value, std::regex("([ ]|[:])"), "$$$1"); // "$$" converts to "$" symbol, and $1 captures 1 match.
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

void NinjaWriter::WriteFile(const std::string &targetRules, bool verbose) const
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



	const std::string buildNinja = ninjaHeader.str() + identsDecl.str() + targetRules;
	if (verbose)
		std::cout << "\nNinja file:\n" << buildNinja;

	FileInfo(buildRoot + "/build.ninja").WriteFile(buildNinja, false);
}
