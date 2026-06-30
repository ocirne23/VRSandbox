module Script;

import Core;
import Core.Windows;
import Core.Log;

namespace
{
    namespace fs = std::filesystem;

    // Runs a full command line synchronously, no console window. Returns the child exit code, or -1.
    int runProcess(const std::string& cmdLine)
    {
        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<char> buffer(cmdLine.begin(), cmdLine.end());
        buffer.push_back('\0');
        if (!CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            return -1;
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)code;
    }

    std::string readTextFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) return std::string();
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return content;
    }
}

// A loaded module plus the DLL handle/path needed to unload and recompile it.
struct CachedScript
{
    ScriptModule entries;
    void*        module = nullptr; // HMODULE
    std::string  dllPath;
};

struct ScriptHost::Impl
{
    uint32      buildCounter = 0;
    std::string vcvarsPath;                                 // cached after first lookup
    std::unordered_map<std::string, CachedScript> scripts;  // keyed by source path

    // Locates vcvars64.bat via vswhere (once, then cached). Empty string on failure.
    const std::string& findVcvars()
    {
        if (!vcvarsPath.empty()) return vcvarsPath;

        char pfBuffer[512];
        const DWORD len = GetEnvironmentVariableA("ProgramFiles(x86)", pfBuffer, sizeof(pfBuffer));
        const std::string programFiles = (len > 0 && len < sizeof(pfBuffer)) ? std::string(pfBuffer, len) : std::string("C:\\Program Files (x86)");
        const std::string vswhere = programFiles + "\\Microsoft Visual Studio\\Installer\\vswhere.exe";
        if (fs::exists(vswhere))
        {
            const fs::path outDir = fs::current_path() / "Local" / "Scripts";
            std::error_code ec; fs::create_directories(outDir, ec);
            const fs::path outFile = outDir / "vswhere.out";

            const std::string inner = "\"" + vswhere + "\" -latest -prerelease -products * -property installationPath > \"" + outFile.string() + "\"";
            runProcess("cmd.exe /c \"" + inner + "\"");

            std::string installPath = readTextFile(outFile);
            while (!installPath.empty() && (installPath.back() == '\r' || installPath.back() == '\n' || installPath.back() == ' ' || installPath.back() == '\t'))
                installPath.pop_back();

            if (!installPath.empty())
            {
                const std::string vcvars = installPath + "\\VC\\Auxiliary\\Build\\vcvars64.bat";
                if (fs::exists(vcvars)) { vcvarsPath = vcvars; return vcvarsPath; }
            }
        }

        const char* const fallbacks[] = {
            "C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders\\VC\\Auxiliary\\Build\\vcvars64.bat",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
        };
        for (const char* candidate : fallbacks)
            if (fs::exists(candidate)) { vcvarsPath = candidate; return vcvarsPath; }

        Log::error("Script: could not locate vcvars64.bat (vswhere found nothing and no fallback path exists)");
        return vcvarsPath;
    }

    bool compile(const std::string& sourcePath, std::string& outDll, std::string& outErrors)
    {
        const std::string& vcvars = findVcvars();
        if (vcvars.empty()) { outErrors = "MSVC toolchain not found (see log)"; return false; }

        const fs::path assetsDir = fs::current_path();
        const fs::path outDir = assetsDir / "Local" / "Scripts";
        const fs::path includeDir = assetsDir / "Scripts";         // ScriptAPI.h lives here (copied at build)
        std::error_code ec; fs::create_directories(outDir, ec);

        if (!fs::exists(sourcePath)) { outErrors = "Script source not found: " + sourcePath; return false; }

        const std::string base = "script_" + std::to_string(buildCounter++);
        const fs::path dllPath = outDir / (base + ".dll");
        const fs::path objPath = outDir / (base + ".obj");
        const fs::path logPath = outDir / "build.log";
        const fs::path batPath = outDir / "_build.bat";
        const fs::path srcAbs = fs::absolute(sourcePath);

        std::string bat;
        bat += "@echo off\r\n";
        bat += "call \"" + vcvars + "\" >nul 2>&1\r\n";
        bat += "cl /nologo /LD /std:c++20 /MD /O2 /EHsc /arch:AVX2 /wd4100"; // /wd4100: allow unused params (ctx/self/dt)
        bat += " /I\"" + includeDir.string() + "\"";
        bat += " /Fo\"" + objPath.string() + "\"";
        bat += " /Fe\"" + dllPath.string() + "\"";
        bat += " /Tp\"" + srcAbs.string() + "\""; // /Tp: compile as C++ regardless of the .scr extension
        bat += " > \"" + logPath.string() + "\" 2>&1\r\n";
        {
            std::ofstream f(batPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!f.is_open()) { outErrors = "Could not write build script: " + batPath.string(); return false; }
            f.write(bat.data(), bat.size());
        }

        const int code = runProcess("cmd.exe /c \"" + batPath.string() + "\"");
        if (code != 0)
        {
            const std::string log = readTextFile(logPath);
            outErrors = log.empty() ? ("compiler exited with code " + std::to_string(code)) : log;
            return false;
        }
        outDll = dllPath.string();
        return true;
    }

    const ScriptModule* getOrLoad(const std::string& path, bool forceRecompile)
    {
        auto it = scripts.find(path);
        if (it != scripts.end() && !forceRecompile)
            return &it->second.entries;

        std::string dllPath, errors;
        if (!compile(path, dllPath, errors))
        {
            Log::error("Script compile failed (" + path + "):\n" + errors);
            if (it != scripts.end()) return &it->second.entries;             // keep previous build
            return &scripts.emplace(path, CachedScript{}).first->second.entries; // cache the failure
        }

        HMODULE newModule = LoadLibraryA(dllPath.c_str());
        void* update = newModule ? (void*)GetProcAddress(newModule, "ScriptUpdate") : nullptr;
        if (!update)
        {
            Log::error("Script: DLL load/ScriptUpdate failed (" + dllPath + ")");
            if (newModule) FreeLibrary(newModule);
            std::error_code ec; fs::remove(dllPath, ec);
            if (it != scripts.end()) return &it->second.entries;
            return &scripts.emplace(path, CachedScript{}).first->second.entries;
        }

        CachedScript& slot = (it != scripts.end()) ? it->second : scripts.emplace(path, CachedScript{}).first->second;
        if (slot.module) // swap out the previous build
        {
            FreeLibrary((HMODULE)slot.module);
            if (!slot.dllPath.empty()) { std::error_code ec; fs::remove(slot.dllPath, ec); }
        }
        slot.module = newModule;
        slot.dllPath = dllPath;
        slot.entries.update = update;
        slot.entries.init = (void*)GetProcAddress(newModule, "ScriptInit");
        slot.entries.shutdown = (void*)GetProcAddress(newModule, "ScriptShutdown");
        Log::info("Script loaded: " + path);
        return &slot.entries;
    }

    void unloadAll()
    {
        for (auto& [path, script] : scripts)
        {
            if (script.module) FreeLibrary((HMODULE)script.module);
            if (!script.dllPath.empty()) { std::error_code ec; fs::remove(script.dllPath, ec); }
        }
        scripts.clear();
    }
};

ScriptHost::ScriptHost() : m_impl(std::make_unique<Impl>()) {}
ScriptHost::~ScriptHost() { m_impl->unloadAll(); }

const ScriptModule* ScriptHost::getOrLoad(const std::string& path, bool forceRecompile)
{
    return m_impl->getOrLoad(path, forceRecompile);
}

void ScriptHost::shutdown()
{
    m_impl->unloadAll();
}
