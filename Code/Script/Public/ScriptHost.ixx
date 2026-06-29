export module Script;

import Core;

// Host-side owner of runtime-compiled visual scripts. Compiles a script .cpp to a
// self-contained DLL (via the installed MSVC toolchain), hot-swaps it in, and ticks
// it each frame. Mirrors the renderer's shader hot-reload: on a failed recompile the
// previously loaded script keeps running.
export class ScriptHost final
{
public:

    ScriptHost();
    ~ScriptHost();

    // Compiles sourcePath (relative to the Assets working dir) to a DLL and swaps it in.
    // Returns false and leaves any currently loaded script untouched on failure.
    bool reload(const std::string& sourcePath);

    // Invokes the loaded script's ScriptUpdate. No-op when nothing is loaded.
    // Call after Renderer::beginFrame so per-frame submissions (lights, sun) land this frame.
    void tick(float deltaSec);

    void shutdown();

private:

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
