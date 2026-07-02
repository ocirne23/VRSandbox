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

    // Canonical cache key for a script. The same file reaches getOrLoad spelled different ways — the prefab
    // stores a relative "Scripts/Foo.scr", the asset browser hands over an absolute backslash path — so key
    // by the resolved absolute path. Otherwise a panel reload and the owning entity land in different slots
    // and the entity never sees the recompile (hot reload silently no-ops).
    std::string cacheKey(const std::string& path)
    {
        std::error_code ec;
        const fs::path resolved = fs::weakly_canonical(fs::absolute(path), ec);
        return (ec ? fs::absolute(path) : resolved).string();
    }

    struct PdbScan
    {
        int                   maxSerial = -1; // highest serial found on disk (-1 if none)
        std::vector<fs::path> files;          // every "<stem>.<serial>.pdb" in the directory
    };

    // Finds every program PDB named "<stem>.<serial>.pdb" (serial = decimal digits) in `dir`. The compiler
    // PDB "<stem>.building.pdb" is skipped (its middle segment isn't all digits), as is any other script's.
    PdbScan scanPdbs(const fs::path& dir, const std::string& stem)
    {
        PdbScan out;
        const std::string prefix = stem + ".";
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec))
        {
            if (ec) break;
            std::error_code fe;
            if (!it->is_regular_file(fe) || it->path().extension() != ".pdb")
                continue;
            const std::string name = it->path().filename().string();
            if (name.size() < prefix.size() + 5)                         // need at least "<prefix>0.pdb"
                continue;
            if (name.compare(0, prefix.size(), prefix) != 0)
                continue;
            const std::string mid = name.substr(prefix.size(), name.size() - prefix.size() - 4); // strip ".pdb"
            int serial = 0;
            bool digits = !mid.empty();
            for (char c : mid) { if (c < '0' || c > '9') { digits = false; break; } serial = serial * 10 + (c - '0'); }
            if (!digits)
                continue;
            out.files.push_back(it->path());
            if (serial > out.maxSerial) out.maxSerial = serial;
        }
        return out;
    }
}

// A loaded module plus the DLL handle/path needed to unload and recompile it.
struct CachedScript
{
    ScriptModule entries;
    void*        module = nullptr; // HMODULE
    std::string  dllPath;
    std::string  pdbPath;          // program PDB of the loaded build
    int          pdbSerial = -1;   // monotonic build counter; next build uses pdbSerial+1 for a fresh PDB name
};

struct ScriptHost::Impl
{
    std::string vcvarsPath;                                 // cached after first lookup
    std::unordered_map<std::string, CachedScript> scripts;  // keyed by canonical source path
    std::vector<std::string> pendingPdbDeletes;             // superseded PDBs the debugger still holds; retried later

    // Try to delete every superseded PDB; drop the ones now gone. The VS debugger keeps PDBs cached after a
    // module unloads, so a just-superseded PDB often can't be deleted until a later build/shutdown.
    void sweepPendingPdbs()
    {
        std::erase_if(pendingPdbDeletes, [](const std::string& p)
        {
            std::error_code e; fs::remove(p, e);
            return !fs::exists(p, e); // removed (or already gone) -> stop tracking it
        });
    }

    // Best-effort deletion of every "<stem>.<serial>.pdb" in `dir` except `keep` (the live PDB, matched by
    // filename). Includes orphans from earlier runs at any index. Files still locked (typically by the VS
    // debugger) are queued in pendingPdbDeletes for a later sweep.
    void retirePdbs(const fs::path& dir, const std::string& stem, const fs::path& keep)
    {
        const std::string keepName = keep.filename().string();
        for (const fs::path& p : scanPdbs(dir, stem).files)
        {
            if (p.filename().string() == keepName)
                continue;
            std::error_code e; fs::remove(p, e);
            if (fs::exists(p, e))
            {
                const std::string s = p.string();
                bool tracked = false;
                for (const std::string& q : pendingPdbDeletes) if (q == s) { tracked = true; break; }
                if (!tracked) pendingPdbDeletes.push_back(s);
            }
        }
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

    bool compile(const std::string& sourcePath, const fs::path& pdbPath, std::string& outDll, std::string& outErrors)
    {
        const std::string& vcvars = findVcvars();
        if (vcvars.empty()) { outErrors = "MSVC toolchain not found (see log)"; return false; }

        const fs::path assetsDir = fs::current_path();
        const fs::path outDir = assetsDir / "Local" / "Scripts";
        const fs::path includeDir = assetsDir / "Scripts";                          // ScriptAPI.h lives here (copied at build)
        const fs::path glmInclude = assetsDir.parent_path() / "Dependencies" / "Include"; // header-only glm for Vec3 math
        std::error_code ec; fs::create_directories(outDir, ec);

        if (!fs::exists(sourcePath)) { outErrors = "Script source not found: " + sourcePath; return false; }

        // Build to a temp "<stem>.building.dll": the live "<stem>.dll" is file-locked while loaded, so we
        // can't overwrite it directly. getOrLoad frees the old module then renames this temp over it on
        // success, leaving exactly one DLL per script.
        const std::string stem = fs::path(sourcePath).stem().string();
        const std::string base = stem + ".building";
        const fs::path dllPath = outDir / (base + ".dll");
        const fs::path objPath = outDir / (base + ".obj");
        const fs::path compPdb = outDir / (base + ".pdb");  // compiler PDB (temp, discarded)
        const fs::path logPath = outDir / "build.log";
        const fs::path batPath = outDir / "_build.bat";
        const fs::path srcAbs = fs::absolute(sourcePath);

        // /Zi + /Od + /link /DEBUG produce a usable PDB (set breakpoints in the .scr, step, inspect locals).
        // /Od (no optimization) is deliberate: scripts are tiny, and it keeps locals/line info intact. The
        // linker writes the PDB to the caller-chosen ping-pong name (absolute path embedded in the DLL), so
        // a rebuild never targets the PDB the debugger is currently holding open (avoids LNK1201).
        std::string bat;
        bat += "@echo off\r\n";
        bat += "call \"" + vcvars + "\" >nul 2>&1\r\n";
        bat += "cl /nologo /LD /std:c++20 /MD /Od /Zi /EHsc /arch:AVX2 /wd4100 /DSCRIPT_BUILD"; // /wd4100: unused params; /DSCRIPT_BUILD: Entity is the layout mirror
        bat += " /I\"" + includeDir.string() + "\"";
        bat += " /I\"" + glmInclude.string() + "\""; // ScriptAPI.h includes <glm/glm.hpp> (Vec3 = glm::vec3)
        bat += " /Fo\"" + objPath.string() + "\"";
        bat += " /Fd\"" + compPdb.string() + "\"";
        bat += " /Fe\"" + dllPath.string() + "\"";
        bat += " /Tp\"" + srcAbs.string() + "\""; // /Tp: compile as C++ regardless of the .scr extension
        bat += " /link /DEBUG /INCREMENTAL:NO /PDB:\"" + pdbPath.string() + "\"";
        bat += " > \"" + logPath.string() + "\" 2>&1\r\n";
        {
            std::ofstream f(batPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!f.is_open()) { outErrors = "Could not write build script: " + batPath.string(); return false; }
            f.write(bat.data(), bat.size());
        }

        const int code = runProcess("cmd.exe /c \"" + batPath.string() + "\"");

        // cl drops <base>.obj/.pdb and the linker <base>.lib/.exp next to the DLL; clear those intermediates.
        // The kept artifacts are "<stem>.dll" (after rename) and "<stem>.pdb".
        for (const char* ext : { ".obj", ".lib", ".exp", ".pdb" })
        {
            std::error_code e; fs::remove(outDir / (base + ext), e);
        }

        if (code != 0)
        {
            const std::string log = readTextFile(logPath);
            outErrors = log.empty() ? ("compiler exited with code " + std::to_string(code)) : log;
            std::error_code e; fs::remove(dllPath, e); // drop any half-written temp DLL
            return false;
        }
        outDll = dllPath.string();
        return true;
    }

    // The single persistent DLL a script compiles to (matches compile()'s final destination).
    fs::path scriptDllPath(const std::string& sourcePath) const
    {
        return fs::current_path() / "Local" / "Scripts" / (fs::path(sourcePath).stem().string() + ".dll");
    }

    // LoadLibrary `dll` and resolve the entry points into `slot`, freeing any module it previously held.
    bool loadDll(CachedScript& slot, const fs::path& dll)
    {
        HMODULE m = LoadLibraryA(dll.string().c_str());

        void* update = m ? (void*)GetProcAddress(m, "Update") : nullptr;
        void* onSpawn = m ? (void*)GetProcAddress(m, "OnSpawn") : nullptr;
        void* onDestroy = m ? (void*)GetProcAddress(m, "OnDestroy") : nullptr;
        void* onEvent = m ? (void*)GetProcAddress(m, "OnEvent") : nullptr;

        if (!update && !onSpawn && !onDestroy && !onEvent) { if (m) FreeLibrary(m); return false; }
        if (slot.module) FreeLibrary((HMODULE)slot.module);
        slot.module = m;
        slot.dllPath = dll.string();
        slot.entries.dllPath = dll.string();
        slot.entries.update = update;
        slot.entries.onSpawn = onSpawn;
        slot.entries.onDestroy = onDestroy;
        slot.entries.onEvent = onEvent;
        slot.entries.dataSize = 0;
        if (auto sizeFn = (uint32(*)())GetProcAddress(m, "ScriptDataSize"))
            slot.entries.dataSize = sizeFn();

        // Resolve On Event entry names once at load, indexed the same way OnEvent(eventIdx) expects, so the
        // host maps a fired event name to its index without the script ever seeing a string.
        slot.entries.eventIndexes.clear();
        if (auto countFn = (int(*)())GetProcAddress(m, "ScriptEventCount"))
            if (auto nameFn = (const char*(*)(int))GetProcAddress(m, "ScriptEventName"))
            {
                const int count = countFn();
                slot.entries.eventIndexes.reserve(count > 0 ? count : 0);
                for (int i = 0; i < count; ++i)
                    slot.entries.eventIndexes.emplace(nameFn(i), i);
            }
        return true;
    }

    const ScriptModule* getOrLoad(const std::string& path, bool forceRecompile)
    {
        const std::string key = cacheKey(path);
        auto it = scripts.find(key);
        if (it != scripts.end() && !forceRecompile)
            return &it->second.entries;

        // First load this session and not forced: if an existing DLL's mtime matches the source's (we stamp
        // it to match after each compile), the source is unchanged since it was built — load it, skip cl.
        if (!forceRecompile && it == scripts.end())
        {
            const fs::path dll = scriptDllPath(path);
            std::error_code es, ed;
            const auto srcTime = fs::last_write_time(path, es);
            const auto dllTime = fs::last_write_time(dll, ed);
            if (!es && !ed && srcTime == dllTime)
            {
                CachedScript& slot = scripts.emplace(key, CachedScript{}).first->second;
                slot.entries.scriptPath = path;
                if (loadDll(slot, dll))
                {
                    Log::info("Script unchanged; loaded cached DLL: " + path);
                    return &slot.entries;
                }
                scripts.erase(key); // cached DLL was unusable; fall through and recompile
            }
        }

        it = scripts.find(key);

        // Each build writes a FRESH, never-reused program PDB "<stem>.<serial>.pdb". The VS debugger caches
        // PDBs even after a module unloads, so reusing a name (even ping-ponging two) eventually collides
        // with a held handle (LNK1201). Name the new PDB with a serial ABOVE every "<stem>.<N>.pdb" already
        // on disk — not just the last one this session made: a prior run (or a build whose PDB the debugger
        // still holds) can leave orphans at any index, and picking max+1 guarantees the linker never targets
        // a name still open, so a delete that failed earlier can never block this build.
        const fs::path outDir = scriptDllPath(path).parent_path();
        const std::string stem = fs::path(path).stem().string();
        int serial = scanPdbs(outDir, stem).maxSerial + 1;
        if (it != scripts.end() && it->second.pdbSerial + 1 > serial)
            serial = it->second.pdbSerial + 1;
        const fs::path pdbPath = outDir / (stem + "." + std::to_string(serial) + ".pdb");

        std::string tempDll, errors;
        if (!compile(path, pdbPath, tempDll, errors))
        {
            Log::error("Script compile failed (" + path + "):\n" + errors);
            if (it != scripts.end()) return &it->second.entries;             // keep previous build
            ScriptModule* scriptModule = &scripts.emplace(key, CachedScript{}).first->second.entries; // cache the failure
            scriptModule->scriptPath = path;
            return scriptModule;
        }

        // Compile succeeded: free the previous build (unlocking its <stem>.dll) and promote the temp DLL
        // into its place, so each script keeps exactly one DLL on disk.
        CachedScript& slot = (it != scripts.end()) ? it->second : scripts.emplace(key, CachedScript{}).first->second;
        slot.entries.scriptPath = path;
        if (slot.module) { FreeLibrary((HMODULE)slot.module); slot.module = nullptr; }

        const fs::path finalDll = scriptDllPath(path);
        std::error_code ec;
        fs::remove(finalDll, ec);                 // the old build is freed, so this unlinks cleanly
        fs::rename(tempDll, finalDll, ec);
        if (ec)
        {
            std::error_code e; fs::remove(tempDll, e);
            Log::error("Script: could not place DLL (" + finalDll.string() + "): " + ec.message());
            slot.entries = ScriptModule{}; slot.dllPath.clear();
            return &slot.entries;
        }

        // Stamp the DLL with the source's mtime (before loading, while the file is still unlocked) so a
        // later startup recognises an unchanged script and reuses this DLL instead of recompiling.
        std::error_code te;
        const auto srcTime = fs::last_write_time(path, te);
        if (!te) fs::last_write_time(finalDll, srcTime, te);

        if (!loadDll(slot, finalDll))
        {
            Log::info("Script: DLL load failed, no exported symbols (" + finalDll.string() + ")");
            fs::remove(finalDll, ec);
            slot.entries = ScriptModule{}; slot.dllPath.clear();
            retirePdbs(outDir, stem, pdbPath);
            sweepPendingPdbs();
            return &slot.entries;
        }

        // Retire every PDB except the one just loaded: the previous build's plus any orphans left by earlier
        // runs (higher-indexed included). Ones the debugger still holds get queued for a later sweep.
        retirePdbs(outDir, stem, pdbPath);
        slot.pdbPath = pdbPath.string();
        slot.pdbSerial = serial;
        sweepPendingPdbs();

        Log::info("Script loaded: " + path);
        return &slot.entries;
    }

    void unloadAll()
    {
        const fs::path outDir = fs::current_path() / "Local" / "Scripts";
        for (auto& [path, script] : scripts)
        {
            if (script.module) FreeLibrary((HMODULE)script.module);
            if (!script.dllPath.empty()) { std::error_code ec; fs::remove(script.dllPath, ec); }
            // Delete this script's PDBs — the live one plus any orphans on disk (keep nothing).
            retirePdbs(outDir, fs::path(path).stem().string(), fs::path());
        }
        scripts.clear();
        sweepPendingPdbs();
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
