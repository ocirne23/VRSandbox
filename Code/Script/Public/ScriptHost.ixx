export module Script;

import Core;
import Entity;

// Host-side owner of runtime-compiled visual scripts. Compiles a script (.scr) to a self-contained DLL
// (via the installed MSVC toolchain) and runs it. Scripts are cached by path; recompiling one swaps the
// DLL transparently for every user (the panel test-script and any entity referencing it). Mirrors the
// renderer's shader hot-reload: on a failed recompile the previously loaded version keeps running.
export class ScriptHost final
{
public:

    ScriptHost();
    ~ScriptHost();

    // (Re)compiles sourcePath and makes it the active "global" test script. Returns false (keeping the
    // previous build) on failure.
    bool reload(const std::string& sourcePath);

    // Invokes the active global script's ScriptUpdate with no entity (self == null). No-op when none.
    void tick(float deltaSec);

    // Walks the entity tree; every entity with an enabled ScriptComponent runs its referenced script with
    // that entity as `self`, so its Get/Set Entity nodes read and write that entity. Lazily compiles each
    // referenced script. Call after Renderer::beginFrame.
    void tickEntities(const std::vector<EntityPtr>& roots, float deltaSec);

    void shutdown();

private:

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
