#include "CommandLine.h"
#include "VariableMap.h"

#include <filesystem>
namespace fs = std::filesystem;
using fserr  = std::error_code;

#include <iostream>

namespace {
const std::string g_buildOption           = "--build";
const std::string g_ninjaOption           = "--ninja";
const std::string g_cmakeOption           = "--cmake";
const std::string g_dryOption             = "--dry";
const std::string g_verboseOption         = "--verbose";
const std::string g_depsOption            = "--deps";             // e.g. --deps InstallProduct=Release_InstallBuildFixupRelease
const std::string g_preferredConfigOption = "--preferred-config"; // e.g. --preferred-config Debug
}

CommandLine::CommandLine(int argc, char* argv[])
{
    std::string preferredConfig;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == g_buildOption && i < argc - 1)
            rootDir = argv[i + 1];
        else if (arg == g_ninjaOption && i < argc - 1)
            ninjaExe = argv[i + 1];
        else if (arg == g_cmakeOption && i < argc - 1)
            cmakeExe = argv[i + 1];
        else if (arg == g_preferredConfigOption && i < argc - 1)
            preferredConfig = argv[i + 1];
        else if (arg == g_dryOption)
            dryRun = true;
        else if (arg == g_verboseOption)
            verbose = true;
        else if (arg == g_depsOption && i < argc - 1) {
            auto depsPair = strToList(argv[i + 1], '=');
            if (depsPair.size() == 2)
                additionalDeps[depsPair[0]] = strToList(depsPair[1], ',');
        }
    }
    if (rootDir.empty() || ninjaExe.empty() || cmakeExe.empty()) {
        throw std::invalid_argument("usage: --build <msbuild directory> --ninja <ninja binary> --cmake <cmake binary> [--dry] [--verbose] [--deps target=target1,target2... ] ");
    }
    if (dryRun)
        std::cout << "Dry run.\n";
    if (preferredConfig == "Debug")
        configs = StringVector{ "Debug", "Release" };

    StringVector slnFiles;
    for (const fs::directory_entry& it : fs::directory_iterator(rootDir)) {
        const fs::path& p = it.path();
        if (fs::is_regular_file(p) && p.extension() == ".sln")
            slnFiles.push_back(p.filename().u8string());
    }
    if (slnFiles.size() != 1)
        throw std::invalid_argument("directory should contain exactly one sln file");

    std::replace(rootDir.begin(), rootDir.end(), '/', '\\');
    slnFile = slnFiles[0];
}
