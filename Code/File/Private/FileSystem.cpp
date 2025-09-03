module File.FileSystem;

import Core;
import Core.Windows;

bool FileSystem::initialize()
{
    constexpr const char* ASSETS_DIR = "/Assets/";
    constexpr const char* DLL_DIR = "/Dependencies/Dll/";

    const std::filesystem::path currDir = std::filesystem::current_path();
    std::filesystem::path rootDir = currDir;
    while (!std::filesystem::exists(std::filesystem::path(rootDir.string() + ASSETS_DIR)))
    {
        if (!rootDir.has_parent_path())
        {
            assert(false && "Could not find project root directory");
            return false;
        }
        rootDir = rootDir.parent_path();
    }
    std::filesystem::path assetsDir = std::filesystem::path(rootDir.string() + ASSETS_DIR);
    std::filesystem::current_path(assetsDir);
    std::filesystem::path dllDir = std::filesystem::path(rootDir.string() + DLL_DIR);
    if (std::filesystem::exists(dllDir))
    {
        AddDllDirectory(dllDir.wstring().c_str());
    }
    return true;
}

std::string FileSystem::readFileStr(const std::string& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);

    if (!file.is_open())
        return std::string();

    std::ifstream::pos_type fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string fileContent(fileSize, '\0');
    file.read(fileContent.data(), fileSize);
    return fileContent;
}