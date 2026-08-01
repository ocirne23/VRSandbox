module;

#include <intrin.h> // __rdtsc for the inline scope hot path

export module Profiling:Profiler;

import Core;

// Category a scope belongs to (which library / kind of code it measures); drives the timeline
// colors and the stats-table grouping. Wait is for parked/blocked time, GPU for renderer GPU passes.
export enum class EProfileCategory : uint8
{
    App, Entity, Script, Animation, Physics, Audio, Particle, Force, Spatial,
    Threading, Procedural, Network, File, Renderer, GPU, UI, Input, Wait, Other,
    Count
};

export constexpr const char* profileCategoryName(EProfileCategory category)
{
    constexpr const char* names[(uint32)EProfileCategory::Count] = {
        "App", "Entity", "Script", "Animation", "Physics", "Audio", "Particle", "Force", "Spatial",
        "Threading", "Procedural", "Network", "File", "Renderer", "GPU", "UI", "Input", "Wait", "Other",
    };
    return names[(uint32)category];
}

// Packed ABGR (what ImGui's IM_COL32 produces: a<<24|b<<16|g<<8|r), usable directly as an ImU32.
export constexpr uint32 profileCategoryColor(EProfileCategory category)
{
    constexpr uint32 colors[(uint32)EProfileCategory::Count] = {
        0xFFB0B0B0, // App        - light gray
        0xFF3C96E8, // Entity     - orange
        0xFFE86BA4, // Script     - purple
        0xFFB46BE8, // Animation  - pink
        0xFF5B5BE8, // Physics    - red
        0xFFB4C83C, // Audio      - teal
        0xFF3CD4E8, // Particle   - yellow
        0xFFE8B446, // Force      - cyan
        0xFF64C864, // Spatial    - green
        0xFF2878C8, // Threading  - dark orange
        0xFF46B4A0, // Procedural - olive
        0xFFE8785A, // Network    - blue
        0xFF5080A0, // File       - brown
        0xFFDCA050, // Renderer   - light blue
        0xFF78DC46, // GPU        - vivid green
        0xFFC850C8, // UI         - magenta
        0xFF50DC8C, // Input      - light green
        0xFF706860, // Wait       - dark gray
        0xFF909090, // Other      - gray
    };
    return colors[(uint32)category];
}

// One completed scope. Written ONCE at scope exit (so pushes are naturally ordered by END time,
// which the snapshot's early-out relies on). name must be a string LITERAL - stored by pointer.
export struct ProfileRecord
{
    uint64 start = 0; // profiler ticks (Profiler::tick domain)
    uint64 end = 0;
    const char* name = nullptr;
    uint16 depth = 0;
    uint8 category = 0; // EProfileCategory
    uint8 _pad0 = 0;
    uint32 _pad1 = 0;
};
static_assert(sizeof(ProfileRecord) == 32);

// A single-writer/many-reader ring of ProfileRecords: one per registered thread, plus named tracks
// (the renderer's GPU track). The owner writes records + a monotonic release cursor; readers
// (Profiler::snapshotTrack, main thread) copy behind the cursor and detect being lapped. NEVER
// written by two threads at once - a thread track is written only by its thread, a named track only
// by whoever created it (the GPU track: the main thread, at collect time).
export class ProfileTrack final
{
public:
    static constexpr uint32 CAPACITY = 1 << 15; // records; 1 MiB per track, ring overwrite when full

    void initialize(const char* name, uint32 threadId, uint32 sortKey)
    {
        m_records = std::make_unique<ProfileRecord[]>(CAPACITY);
        setName(name);
        m_threadId = threadId;
        m_sortKey = sortKey;
    }

    void push(uint64 start, uint64 end, const char* name, uint16 depth, EProfileCategory category)
    {
        const uint64 idx = m_cursor.load(std::memory_order_relaxed);
        ProfileRecord& record = m_records[idx & (CAPACITY - 1)];
        record.start = start;
        record.end = end;
        record.name = name;
        record.depth = depth;
        record.category = (uint8)category;
        m_cursor.store(idx + 1, std::memory_order_release); // publish: readers acquire the cursor
    }

    void setName(const char* name)
    {
        uint32 i = 0;
        for (; name[i] != 0 && i < sizeof(m_name) - 1; ++i)
            m_name[i] = name[i];
        m_name[i] = 0;
    }
    void setSortKey(uint32 sortKey) { m_sortKey = sortKey; }

    const char* getName() const { return m_name; }
    uint32 getThreadId() const { return m_threadId; }
    uint32 getSortKey() const { return m_sortKey; }
    uint64 getCursor() const { return m_cursor.load(std::memory_order_acquire); }
    const ProfileRecord& getRecord(uint64 absIdx) const { return m_records[absIdx & (CAPACITY - 1)]; }

    uint32 m_openDepth = 0; // owner-thread scope nesting depth (touched only by ProfileScope on the owner)

private:

    std::unique_ptr<ProfileRecord[]> m_records;
    std::atomic<uint64> m_cursor = 0;
    char m_name[48] = {};
    uint32 m_threadId = 0;
    uint32 m_sortKey = 2; // display order: Profiler::SORT_KEY_* (main, GPU, workers by index, lazy threads last)
};

// Global CPU (+GPU track) profiler. Scope markers record into per-thread lock-free rings; the clock
// is __rdtsc (invariant TSC), calibrated against QPC every endFrame so GPU timestamps (which
// calibrate against QPC) and millisecond conversions stay accurate. Recording is always on unless
// setEnabled(false); the UI panel does its own pause/snapshot on top. All read APIs (snapshot,
// frame marks, conversions) are main-thread.
export class Profiler final
{
public:
    static constexpr uint32 MAX_TRACKS = 64;
    static constexpr uint32 FRAME_HISTORY = 512; // frame boundary ring for the UI frame graph

    void initialize(); // main thread, early in main() - registers the calling thread as "Main"
    void endFrame();   // once per frame from the main loop: frame mark + clock re-anchor

    bool isEnabled() const { return m_enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool enabled) { m_enabled.store(enabled, std::memory_order_relaxed); }

    static uint64 tick() { return __rdtsc(); }

    // The calling thread's track, or nullptr when the thread never registered. Registration is
    // ALWAYS explicit - there is deliberately no lazy first-scope fallback, so a scope on an
    // unregistered thread is a debug assert instead of a mystery track with a raced name.
    ProfileTrack* threadTrack();

    // Registers the calling thread with an explicit name + display order - call at THREAD STARTUP
    // on every thread that will profile (main does it in initialize(), JobSystem workers in
    // workerMain; a new engine thread that wants markers must do the same). Registering here (not
    // on first scope) means no name race with a SetThreadDescription from the spawning thread, no
    // one-time registration cost polluting the first profiled scope, and a deterministic track
    // order in the UI. Renames in place if the thread already registered. Returns nullptr past
    // MAX_TRACKS; the pointer is stable for the process lifetime.
    ProfileTrack* registerThread(const char* name, uint32 sortKey = SORT_KEY_WORKER);

    static constexpr uint32 SORT_KEY_MAIN = 0;
    static constexpr uint32 SORT_KEY_NAMED = 1;        // GPU track
    static constexpr uint32 SORT_KEY_WORKER = 2;       // + worker index
    static constexpr uint32 SORT_KEY_BACKGROUND = 100; // + offset: service threads (streamers, timer, loaders)

    // A track not bound to a thread (the renderer's GPU timeline). Caller owns pushing records into
    // it from ONE thread, with end-ordered pushes (sort before pushing a batch).
    ProfileTrack* createNamedTrack(const char* name, uint32 sortKey = 1);

    // ---- Clock conversions (valid after initialize) ----
    double getMsPerTick() const { return m_msPerTick; }
    double getTicksPerMs() const { return m_ticksPerMs; }
    uint64 ticksFromQpc(uint64 qpc) const // maps a QueryPerformanceCounter value into tick() domain
    {
        return m_tickAnchor + (int64)((double)((int64)qpc - (int64)m_qpcAnchor) * m_ticksPerQpc);
    }

    // ---- Frame history (main thread) ----
    uint64 getFrameCount() const { return m_frameCount; } // completed endFrame calls
    uint64 getFrameMark(uint64 frameIdx) const { return m_frameMarks[frameIdx % FRAME_HISTORY]; } // tick at END of frame frameIdx

    // ---- Track reading (main thread) ----
    uint32 getNumTracks() const { return m_numTracks.load(std::memory_order_acquire); }
    const ProfileTrack& getTrack(uint32 idx) const { return *m_tracks[idx]; }

    // Copies every record overlapping [tMin, tMax] into out (unspecified order: newest-first).
    // Returns false (out cleared) if the writer lapped the ring mid-copy - retry next frame.
    bool snapshotTrack(uint32 trackIdx, uint64 tMin, uint64 tMax, std::vector<ProfileRecord>& out) const;

private:

    ProfileTrack* registerTrack(const char* name, uint32 threadId, uint32 sortKey);

    std::array<std::unique_ptr<ProfileTrack>, MAX_TRACKS> m_tracks;
    std::atomic<uint32> m_numTracks = 0;
    std::atomic<uint32> m_registerLock = 0; // registration-only spinlock
    std::atomic<bool> m_enabled = true;

    std::array<uint64, FRAME_HISTORY> m_frameMarks = {};
    uint64 m_frameCount = 0;

    // rdtsc <-> QPC calibration: ratio estimated over the whole run (converges), anchor refreshed
    // every endFrame so QPC->tick conversions never extrapolate far.
    double m_msPerTick = 0.0;
    double m_ticksPerMs = 0.0;
    double m_ticksPerQpc = 0.0;
    uint64 m_qpcFreq = 0;
    uint64 m_tickAnchor = 0;
    uint64 m_qpcAnchor = 0;
    uint64 m_tickAnchor0 = 0;
    uint64 m_qpcAnchor0 = 0;
};

export namespace Globals
{
    Profiler profiler;
}

// RAII scope marker - the thing user code sprinkles into functions:
//     ProfileScope scope("SkinPalettes", EProfileCategory::Animation);
// ~15ns hot path: two rdtsc, one thread-local read, one 32-byte ring store. name MUST be a string
// literal (stored by pointer). MUST NOT span a JobSystem wait(): the fiber can resume on another
// thread and the destructor would write another thread's ring (debug-asserted). The thread must
// have called Profiler::registerThread at startup (debug-asserted; no-op in release).
export class ProfileScope final
{
public:
    ProfileScope(const char* name, EProfileCategory category)
    {
        if (!Globals::profiler.isEnabled()) [[unlikely]]
        {
            m_track = nullptr;
            return;
        }
        m_track = Globals::profiler.threadTrack();
        assert(m_track != nullptr && "ProfileScope on an unregistered thread - call Globals::profiler.registerThread() at thread startup");
        if (m_track == nullptr) [[unlikely]]
            return;
        m_name = name;
        m_category = category;
        m_depth = (uint16)m_track->m_openDepth++;
        m_start = Profiler::tick();
    }
    // Ends the scope early (for spans that can't be a plain block because declarations in the
    // middle are needed afterwards). The destructor is then a no-op.
    void stop()
    {
        if (m_track == nullptr)
            return;
        const uint64 end = Profiler::tick();
        assert(m_track == Globals::profiler.threadTrack() && "ProfileScope must not span a JobSystem wait (fiber migrated threads)");
        m_track->m_openDepth--;
        m_track->push(m_start, end, m_name, m_depth, m_category);
        m_track = nullptr;
    }
    ~ProfileScope() { stop(); }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:

    ProfileTrack* m_track;
    const char* m_name;
    uint64 m_start;
    uint16 m_depth;
    EProfileCategory m_category;
};
