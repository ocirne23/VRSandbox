export module Script;

import Core;

// Compiled-and-loaded script DLL: the raw entry-point function pointers. Typed as void* so this library
// stays decoupled from the script ABI (ScriptAPI.h) and from the engine — the caller (Entity) owns the
// ScriptContext and casts these. A null `update` marks a script that failed to compile.
export struct ScriptModule
{
    std::string dllPath;
    std::string scriptPath;
    void* onSpawn = nullptr;     // ScriptInitFn
    void* update = nullptr;   // ScriptUpdateFn
    void* onDestroy = nullptr; // ScriptShutdownFn
    void* onEvent = nullptr;  // ScriptOnEventFn (null = script declares no On Event entries)
    uint32 dataSize = 0;      // bytes of persistent ScriptData the script declares (0 = none), from ScriptDataSize()
    std::unordered_map<std::string, int32> eventIndexes; // EventName -> Idx mapping
};

// Compiles visual scripts (.scr) to self-contained DLLs via the installed MSVC toolchain and caches them
// by path. Pure compile/load only: it knows nothing about the engine — no renderer, entity or input.
export class ScriptHost final
{
public:

    ScriptHost();
    ~ScriptHost();

    // Returns the cached module for `path`, compiling it on first use (or when forced). Returns null and
    // keeps the previous build on failure; first-time failures are cached so they aren't retried each frame.
    // The returned pointer is stable until shutdown().
    const ScriptModule* getOrLoad(const std::string& path, bool forceRecompile = false);

    void shutdown();

private:

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

export namespace Globals
{
    ScriptHost scriptHost;
}
