module;

#include "ScriptAPI.h"

module Script;

import Core;
import Core.Windows;
import Core.Log;
import Core.Time;
import Core.glm;
import RendererVK;
import Input;

namespace
{
    namespace fs = std::filesystem;

    // ---- context thunks -----------------------------------------------------
    // These bridge the script's plain-C calls to the real engine Globals:: singletons.

    void thunk_log(const char* message) { Log::info(message ? message : ""); }
    float thunk_deltaSeconds(void) { return (float)Globals::time.getDeltaSec(); }
    float thunk_elapsedSeconds(void) { return (float)Globals::time.getElapsedSec(); }
    int thunk_isKeyDown(int sdlScancode) { return Globals::input.isKeyDown((SDL_Scancode)sdlScancode) ? 1 : 0; }

    void thunk_spawnPointLight(ScriptVec3 position, float range, ScriptVec3 color, float intensity)
    {
        Globals::rendererVK.addPointLight(PointLight(glm::vec3(position.x, position.y, position.z), range,
            glm::vec3(color.x, color.y, color.z), intensity));
    }

    void thunk_setSun(ScriptVec3 direction, ScriptVec3 color, float intensity)
    {
        Globals::rendererVK.setSunLight(glm::vec3(direction.x, direction.y, direction.z),
            glm::vec3(color.x, color.y, color.z), intensity);
    }

    // ---- process helpers ----------------------------------------------------

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

struct ScriptHost::Impl
{
    void*            module = nullptr;     // HMODULE of the currently loaded script
    ScriptContext    context{};
    ScriptUpdateFn   updateFn = nullptr;
    ScriptShutdownFn shutdownFn = nullptr;
    bool             contextBound = false;
    uint32           buildCounter = 0;
    std::string      loadedDllPath;        // deleted when swapped out
    std::string      vcvarsPath;           // cached after first lookup

    void bindContext()
    {
        context.log = &thunk_log;
        context.deltaSeconds = &thunk_deltaSeconds;
        context.elapsedSeconds = &thunk_elapsedSeconds;
        context.isKeyDown = &thunk_isKeyDown;
        context.spawnPointLight = &thunk_spawnPointLight;
        context.setSun = &thunk_setSun;
        contextBound = true;
    }

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

            // -prerelease so Insiders/preview installs are found. cmd /c "" wraps the inner
            // (quoted) command so cmd strips only the outer pair.
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

        // Fallback: known install locations (vswhere missing or returned nothing).
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

    // Compiles sourcePath to a uniquely-named DLL. Returns true and fills outDll on success;
    // fills outErrors with the compiler output on failure.
    bool compile(const std::string& sourcePath, std::string& outDll, std::string& outErrors)
    {
        const std::string& vcvars = findVcvars();
        if (vcvars.empty()) { outErrors = "MSVC toolchain not found (see log)"; return false; }

        const fs::path assetsDir = fs::current_path();             // Assets/
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

        // A .bat sidesteps cmd's nested-quote parsing entirely; cl output is redirected to build.log.
        std::string bat;
        bat += "@echo off\r\n";
        bat += "call \"" + vcvars + "\" >nul 2>&1\r\n";
        bat += "cl /nologo /LD /std:c++20 /MD /O2 /EHsc /arch:AVX2";
        bat += " /I\"" + includeDir.string() + "\"";
        bat += " /Fo\"" + objPath.string() + "\"";
        bat += " /Fe\"" + dllPath.string() + "\"";
        bat += " \"" + srcAbs.string() + "\"";
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

    void unload()
    {
        if (!module) return;
        if (shutdownFn) shutdownFn(&context);
        FreeLibrary((HMODULE)module);
        module = nullptr;
        updateFn = nullptr;
        shutdownFn = nullptr;
        if (!loadedDllPath.empty()) { std::error_code ec; fs::remove(loadedDllPath, ec); loadedDllPath.clear(); }
    }
};

ScriptHost::ScriptHost() : m_impl(std::make_unique<Impl>()) {}
ScriptHost::~ScriptHost() { m_impl->unload(); }

bool ScriptHost::reload(const std::string& sourcePath)
{
    if (!m_impl->contextBound) m_impl->bindContext();

    std::string dllPath, errors;
    if (!m_impl->compile(sourcePath, dllPath, errors))
    {
        Log::error("Script compile failed (" + sourcePath + "):\n" + errors);
        return false;  // keep any currently loaded script running
    }

    HMODULE newModule = LoadLibraryA(dllPath.c_str());
    if (!newModule)
    {
        Log::error("Script: failed to load DLL " + dllPath);
        std::error_code ec; fs::remove(dllPath, ec);
        return false;
    }

    auto initFn = (ScriptInitFn)GetProcAddress(newModule, "ScriptInit");
    auto updateFn = (ScriptUpdateFn)GetProcAddress(newModule, "ScriptUpdate");
    auto shutdownFn = (ScriptShutdownFn)GetProcAddress(newModule, "ScriptShutdown");
    if (!updateFn)
    {
        Log::error("Script: DLL is missing required export ScriptUpdate (" + dllPath + ")");
        FreeLibrary(newModule);
        std::error_code ec; fs::remove(dllPath, ec);
        return false;
    }

    m_impl->unload();                       // shut down + free the previous script
    m_impl->module = newModule;
    m_impl->updateFn = updateFn;
    m_impl->shutdownFn = shutdownFn;
    m_impl->loadedDllPath = dllPath;
    if (initFn) initFn(&m_impl->context);

    Log::info("Script loaded: " + sourcePath);
    return true;
}

void ScriptHost::tick(float deltaSec)
{
    if (m_impl->updateFn) m_impl->updateFn(&m_impl->context, deltaSec);
}

void ScriptHost::shutdown()
{
    m_impl->unload();
}
