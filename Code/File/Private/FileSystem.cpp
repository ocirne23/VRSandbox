module;

#include <iostream>
#include <filesystem>
#include <Windows.h>

module File.FileSystem;

bool FileSystem::initialize()
{
	std::filesystem::path currDir = std::filesystem::current_path();
	std::filesystem::path rootDir = currDir.parent_path().parent_path().parent_path();
	std::filesystem::path assetsDir = std::filesystem::path(rootDir.string() + "/Assets/");
	std::filesystem::current_path(assetsDir);
	std::filesystem::path dllDir = std::filesystem::path(rootDir.string() + "/Dependencies/Dll/");
	AddDllDirectory(dllDir.wstring().c_str());
	return true;
}