export module Script;

import Core;

// Compiled-and-loaded script DLL: the raw entry-point function pointers. Typed as void* so this library
// stays decoupled from the script ABI (ScriptAPI.h) and from the engine — the caller (Entity) owns the
// ScriptContext and casts these. A null `update` marks a script that failed to compile.
export struct ScriptModule
{
    std::string dllPath;
    std::string scriptPath;
    // Do not cache these pointers, they can get swapped when scripts reload!
    void* onSpawn = nullptr;   // ScriptInitFn
    void* update = nullptr;    // ScriptUpdateFn
    void* onDestroy = nullptr; // ScriptShutdownFn
    void* onEvent = nullptr;   // ScriptOnEventFn
    uint32 dataSize = 0;       // bytes of persistent ScriptData the script declares (0 = none), from ScriptDataSize()
	std::vector<std::string> eventNames; // in the order the script declared them

	// Global EventKey -> this script's local OnEvent index (the position its compiled OnEvent switch expects).
	// Owned/filled by ScriptEventManager in onScriptLoadedCallback and rebuilt on every reload (indices can
	// shift when events are added/removed/reordered); mutable so it can be written through the const ScriptModule*
	// the load callback receives. Lets fireEvent translate a fired key into this script's eventIdx with one lookup.
	mutable std::unordered_map<uint32, int> eventKeyToIndex;
};

export typedef void(*ScriptLoadedCallback)(const ScriptModule* script, const std::vector<std::string>& oldEventNames);

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

private:

    void sweepPendingPdbs();
    void retirePdbs(const std::filesystem::path& dir, const std::string& stem, const std::filesystem::path& keep);
    const std::string& findVcvars();
    bool compile(const std::string& sourcePath, const std::filesystem::path& pdbPath, std::string& outDll, std::string& outErrors);
    std::filesystem::path scriptDllPath(const std::string& sourcePath) const;
    struct CachedScript;
    bool loadDll(CachedScript& slot, const std::filesystem::path& dll);
    void unloadAll();

private:

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    friend class ScriptEventManager;

    ScriptLoadedCallback m_scriptLoadedCallback = nullptr;

    std::string vcvarsPath;                                 // cached after first lookup
    std::unordered_map<std::string, CachedScript> scripts;  // keyed by canonical source path
    std::vector<std::string> pendingPdbDeletes;             // superseded PDBs the debugger still holds; retried later
};

export namespace Globals
{
    ScriptHost scriptHost;
}
