module;

#include "ScriptAPI.h"

module Script;

import Core;
import Core.Windows;
import Core.Log;
import Core.Time;
import Core.glm;
import Core.Sphere;
import RendererVK;
import Input;
import Entity;
import Animation;

namespace
{
    namespace fs = std::filesystem;

    // ---- context thunks -----------------------------------------------------
    // These bridge the script's plain-C calls to the real engine Globals:: singletons and to the
    // entity passed as `self`. All entity thunks tolerate a null handle (global/panel script).

    void thunk_log(const char* message) { Log::info(message ? message : ""); }
    float thunk_deltaSeconds(void) { return (float)Globals::time.getDeltaSec(); }
    float thunk_elapsedSeconds(void) { return (float)Globals::time.getElapsedSec(); }
    int thunk_isKeyDown(const char* keyName) { return Globals::input.isKeyDownByName(keyName) ? 1 : 0; }

    void thunk_spawnPointLight(Vec3 position, float range, Vec3 color, float intensity)
    {
        Globals::rendererVK.addPointLight(PointLight(glm::vec3(position.x, position.y, position.z), range,
            glm::vec3(color.x, color.y, color.z), intensity));
    }

    void thunk_setSun(Vec3 direction, Vec3 color, float intensity)
    {
        Globals::rendererVK.setSunLight(glm::vec3(direction.x, direction.y, direction.z),
            glm::vec3(color.x, color.y, color.z), intensity);
    }

    Entity* asEntity(void* e) { return static_cast<Entity*>(e); }
    Vec3 toScript(const glm::vec3& v) { return Vec3{ v.x, v.y, v.z }; }

    Vec3 thunk_entityGetPosition(void* e) { Entity* en = asEntity(e); return en ? toScript(en->pos) : Vec3{ 0, 0, 0 }; }
    float thunk_entityGetScale(void* e) { Entity* en = asEntity(e); return en ? en->scale : 1.0f; }
    Vec3 thunk_entityGetRotation(void* e) { Entity* en = asEntity(e); return en ? toScript(glm::degrees(glm::eulerAngles(en->rot))) : Vec3{ 0, 0, 0 }; }
    Vec3 thunk_entityGetForward(void* e) { Entity* en = asEntity(e); return en ? toScript(en->rot * glm::vec3(0, 0, -1)) : Vec3{ 0, 0, -1 }; }
    Vec3 thunk_entityGetRight(void* e) { Entity* en = asEntity(e); return en ? toScript(en->rot * glm::vec3(1, 0, 0)) : Vec3{ 1, 0, 0 }; }
    Vec3 thunk_entityGetUp(void* e) { Entity* en = asEntity(e); return en ? toScript(en->rot * glm::vec3(0, 1, 0)) : Vec3{ 0, 1, 0 }; }
    const char* thunk_entityGetName(void* e) { Entity* en = asEntity(e); return en ? en->displayName.c_str() : ""; }

    int thunk_entityGetEnabled(void* e)
    {
        Entity* en = asEntity(e);
        if (!en) return 1;
        SceneComponent* sc = getComponent<SceneComponent>(en);
        return sc ? (sc->enabled ? 1 : 0) : 1;
    }
    int thunk_entityGetChildCount(void* e)
    {
        Entity* en = asEntity(e);
        SceneComponent* sc = en ? getComponent<SceneComponent>(en) : nullptr;
        return sc ? (int)sc->children.size() : 0;
    }
    float thunk_entityGetBoundsRadius(void* e)
    {
        Entity* en = asEntity(e);
        RenderComponent* rc = en ? getComponent<RenderComponent>(en) : nullptr;
        return rc ? rc->node.getWorldBounds().radius : 0.0f;
    }

    void thunk_entitySetPosition(void* e, Vec3 v) { if (Entity* en = asEntity(e)) en->pos = glm::vec3(v.x, v.y, v.z); }
    void thunk_entitySetScale(void* e, float s) { if (Entity* en = asEntity(e)) en->scale = s; }
    void thunk_entitySetRotation(void* e, Vec3 d) { if (Entity* en = asEntity(e)) en->rot = glm::quat(glm::radians(glm::vec3(d.x, d.y, d.z))); }
    void thunk_entitySetEnabled(void* e, int enabled) { if (Entity* en = asEntity(e)) if (SceneComponent* sc = getComponent<SceneComponent>(en)) sc->enabled = enabled != 0; }
    void thunk_entitySetAnimFloat(void* e, const char* p, float v) { if (Entity* en = asEntity(e)) if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setFloat(p, v); }
    void thunk_entitySetAnimBool(void* e, const char* p, int v) { if (Entity* en = asEntity(e)) if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setBool(p, v != 0); }
    void thunk_entitySetAnimTrigger(void* e, const char* p) { if (Entity* en = asEntity(e)) if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setTrigger(p); }

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

// One compiled-and-loaded script DLL. A null updateFn marks a script that failed to compile (cached so
// it isn't retried every frame).
struct LoadedScript
{
    void*            module = nullptr;
    ScriptUpdateFn   updateFn = nullptr;
    ScriptShutdownFn shutdownFn = nullptr;
    std::string      dllPath;
};

struct ScriptHost::Impl
{
    ScriptContext context{};
    bool          contextBound = false;
    uint32        buildCounter = 0;
    std::string   vcvarsPath;                              // cached after first lookup
    std::string   activePath;                             // global test script (tick)
    std::unordered_map<std::string, LoadedScript> scripts; // keyed by source path

    void bindContext()
    {
        context.log = &thunk_log;
        context.deltaSeconds = &thunk_deltaSeconds;
        context.elapsedSeconds = &thunk_elapsedSeconds;
        context.isKeyDown = &thunk_isKeyDown;
        context.spawnPointLight = &thunk_spawnPointLight;
        context.setSun = &thunk_setSun;
        context.self = nullptr;
        context.entityGetPosition = &thunk_entityGetPosition;
        context.entityGetScale = &thunk_entityGetScale;
        context.entityGetRotation = &thunk_entityGetRotation;
        context.entityGetForward = &thunk_entityGetForward;
        context.entityGetRight = &thunk_entityGetRight;
        context.entityGetUp = &thunk_entityGetUp;
        context.entityGetName = &thunk_entityGetName;
        context.entityGetEnabled = &thunk_entityGetEnabled;
        context.entityGetChildCount = &thunk_entityGetChildCount;
        context.entityGetBoundsRadius = &thunk_entityGetBoundsRadius;
        context.entitySetPosition = &thunk_entitySetPosition;
        context.entitySetScale = &thunk_entitySetScale;
        context.entitySetRotation = &thunk_entitySetRotation;
        context.entitySetEnabled = &thunk_entitySetEnabled;
        context.entitySetAnimFloat = &thunk_entitySetAnimFloat;
        context.entitySetAnimBool = &thunk_entitySetAnimBool;
        context.entitySetAnimTrigger = &thunk_entitySetAnimTrigger;
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
        bat += "cl /nologo /LD /std:c++20 /MD /O2 /EHsc /arch:AVX2";
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

    // Returns the cached LoadedScript for `path`, compiling it if missing (or forced). Keeps the previous
    // build on failure; caches a null entry for first-time failures so they aren't retried every frame.
    LoadedScript* ensureLoaded(const std::string& path, bool forceRecompile)
    {
        auto it = scripts.find(path);
        if (it != scripts.end() && !forceRecompile)
            return &it->second;

        std::string dllPath, errors;
        if (!compile(path, dllPath, errors))
        {
            Log::error("Script compile failed (" + path + "):\n" + errors);
            if (it != scripts.end()) return &it->second;             // keep the previous build
            return &scripts.emplace(path, LoadedScript{}).first->second; // cache the failure
        }

        HMODULE newModule = LoadLibraryA(dllPath.c_str());
        ScriptInitFn initFn = newModule ? (ScriptInitFn)GetProcAddress(newModule, "ScriptInit") : nullptr;
        ScriptUpdateFn updateFn = newModule ? (ScriptUpdateFn)GetProcAddress(newModule, "ScriptUpdate") : nullptr;
        ScriptShutdownFn shutdownFn = newModule ? (ScriptShutdownFn)GetProcAddress(newModule, "ScriptShutdown") : nullptr;
        if (!updateFn)
        {
            Log::error("Script: DLL missing ScriptUpdate export (" + dllPath + ")");
            if (newModule) FreeLibrary(newModule);
            std::error_code ec; fs::remove(dllPath, ec);
            if (it != scripts.end()) return &it->second;
            return &scripts.emplace(path, LoadedScript{}).first->second;
        }

        LoadedScript& slot = (it != scripts.end()) ? it->second : scripts.emplace(path, LoadedScript{}).first->second;
        if (slot.module) // swap out the previous build
        {
            if (slot.shutdownFn) { context.self = nullptr; slot.shutdownFn(&context); }
            FreeLibrary((HMODULE)slot.module);
            if (!slot.dllPath.empty()) { std::error_code ec; fs::remove(slot.dllPath, ec); }
        }
        slot.module = newModule;
        slot.updateFn = updateFn;
        slot.shutdownFn = shutdownFn;
        slot.dllPath = dllPath;
        if (initFn) { context.self = nullptr; initFn(&context); }
        Log::info("Script loaded: " + path);
        return &slot;
    }

    void walk(Entity* entity, float deltaSec)
    {
        if (!entity) return;
        SceneComponent* sc = getComponent<SceneComponent>(entity);
        if (sc && !sc->enabled) return; // disabled subtree, like renderTree

        if (ScriptComponent* script = getComponent<ScriptComponent>(entity); script && script->enabled && !script->scriptPath.empty())
            if (LoadedScript* loaded = ensureLoaded(script->scriptPath, false); loaded && loaded->updateFn)
            {
                context.self = entity;
                loaded->updateFn(&context, deltaSec);
            }

        if (sc)
            for (const EntityPtr& child : sc->children)
                walk(child.get(), deltaSec);
    }

    void unloadAll()
    {
        for (auto& [path, script] : scripts)
        {
            if (script.shutdownFn) { context.self = nullptr; script.shutdownFn(&context); }
            if (script.module) FreeLibrary((HMODULE)script.module);
            if (!script.dllPath.empty()) { std::error_code ec; fs::remove(script.dllPath, ec); }
        }
        scripts.clear();
    }
};

ScriptHost::ScriptHost() : m_impl(std::make_unique<Impl>()) {}
ScriptHost::~ScriptHost() { m_impl->unloadAll(); }

bool ScriptHost::reload(const std::string& sourcePath)
{
    if (!m_impl->contextBound) m_impl->bindContext();
    LoadedScript* loaded = m_impl->ensureLoaded(sourcePath, true);
    if (loaded && loaded->updateFn)
    {
        m_impl->activePath = sourcePath;
        return true;
    }
    return false;
}

void ScriptHost::tick(float deltaSec)
{
    auto it = m_impl->scripts.find(m_impl->activePath);
    if (it != m_impl->scripts.end() && it->second.updateFn)
    {
        m_impl->context.self = nullptr;
        it->second.updateFn(&m_impl->context, deltaSec);
    }
}

void ScriptHost::tickEntities(const std::vector<EntityPtr>& roots, float deltaSec)
{
    if (!m_impl->contextBound) m_impl->bindContext();
    for (const EntityPtr& root : roots)
        m_impl->walk(root.get(), deltaSec);
}

void ScriptHost::shutdown()
{
    m_impl->unloadAll();
}
