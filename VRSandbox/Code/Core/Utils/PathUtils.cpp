#include "PathUtils.h"

#include <windows.h>
#include <filesystem>
#include <libloaderapi.h>

namespace PathUtils
{
    void setHardcodedWorkingDir()
    {
        // Set current working directory to root folder
        char buf[512] = {};
        GetModuleFileName(nullptr, buf, sizeof(buf));
        std::filesystem::path rootPath(buf);
        for (int i = 0; i < 4; ++i)
            rootPath = rootPath.parent_path();
        std::filesystem::current_path(rootPath);
        if (!std::filesystem::exists(rootPath.append("resources2.cfg")))
        {
            printf("Could not find resources2.cfg in path: %s, exiting", rootPath.string().c_str());
            __debugbreak();
        }
    }
}