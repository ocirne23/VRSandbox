module Profiling;

import Core;
import Core.Windows;

// The calling thread's track, set only by registerThread (registration is always explicit). Safe
// under /GT because every access re-reads it through the accessor in the same call; the one hazard
// (a ProfileScope spanning a fiber wait) is asserted in the scope destructor instead.
static thread_local ProfileTrack* t_profileTrack = nullptr;

void Profiler::initialize()
{
    LARGE_INTEGER freq, qpc;
    QueryPerformanceFrequency(&freq);
    m_qpcFreq = (uint64)freq.QuadPart;

    QueryPerformanceCounter(&qpc);
    m_qpcAnchor0 = (uint64)qpc.QuadPart;
    m_tickAnchor0 = tick();

    // Short busy-wait (~0.5ms) for a usable initial rdtsc<->QPC ratio; every endFrame afterwards
    // re-estimates it over the whole run, so it only ever gets more accurate.
    const uint64 waitQpcTicks = m_qpcFreq / 2000;
    do { QueryPerformanceCounter(&qpc); } while ((uint64)qpc.QuadPart - m_qpcAnchor0 < waitQpcTicks);
    const uint64 t = tick();

    m_ticksPerQpc = (double)(t - m_tickAnchor0) / (double)((uint64)qpc.QuadPart - m_qpcAnchor0);
    m_tickAnchor = t;
    m_qpcAnchor = (uint64)qpc.QuadPart;
    m_ticksPerMs = m_ticksPerQpc * (double)m_qpcFreq / 1000.0;
    m_msPerTick = 1.0 / m_ticksPerMs;

    registerThread("Main", SORT_KEY_MAIN);
}

void Profiler::endFrame()
{
    const uint64 t = tick();
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);

    const uint64 qpcElapsed = (uint64)qpc.QuadPart - m_qpcAnchor0;
    if (qpcElapsed > 0)
    {
        m_ticksPerQpc = (double)(t - m_tickAnchor0) / (double)qpcElapsed;
        m_ticksPerMs = m_ticksPerQpc * (double)m_qpcFreq / 1000.0;
        m_msPerTick = 1.0 / m_ticksPerMs;
    }
    m_tickAnchor = t;
    m_qpcAnchor = (uint64)qpc.QuadPart;

    m_frameMarks[m_frameCount % FRAME_HISTORY] = t;
    m_frameCount++;
}

ProfileTrack* Profiler::threadTrack()
{
    return t_profileTrack;
}

ProfileTrack* Profiler::registerThread(const char* name, uint32 sortKey)
{
    if (ProfileTrack* track = t_profileTrack; track != nullptr)
    {
        track->setName(name);
        track->setSortKey(sortKey);
        return track;
    }
    ProfileTrack* track = registerTrack(name, (uint32)GetCurrentThreadId(), sortKey);
    t_profileTrack = track;
    return track;
}

ProfileTrack* Profiler::createNamedTrack(const char* name, uint32 sortKey)
{
    return registerTrack(name, 0, sortKey);
}

ProfileTrack* Profiler::registerTrack(const char* name, uint32 threadId, uint32 sortKey)
{
    while (m_registerLock.exchange(1, std::memory_order_acquire) != 0)
        std::this_thread::yield();

    ProfileTrack* track = nullptr;
    const uint32 idx = m_numTracks.load(std::memory_order_relaxed);
    if (idx < MAX_TRACKS)
    {
        m_tracks[idx] = std::make_unique<ProfileTrack>();
        m_tracks[idx]->initialize(name, threadId, sortKey);
        track = m_tracks[idx].get();
        m_numTracks.store(idx + 1, std::memory_order_release); // publish after construction
    }

    m_registerLock.store(0, std::memory_order_release);
    return track;
}

bool Profiler::snapshotTrack(uint32 trackIdx, uint64 tMin, uint64 tMax, std::vector<ProfileRecord>& out) const
{
    out.clear();
    const ProfileTrack& track = *m_tracks[trackIdx];
    const uint64 cursor = track.getCursor();
    const uint64 lo = cursor > ProfileTrack::CAPACITY ? cursor - ProfileTrack::CAPACITY : 0;

    // Pushes happen at scope END, so records are ordered by end time: scan backwards from the
    // newest and stop at the first record that ended before the window.
    uint64 i = cursor;
    while (i > lo)
    {
        --i;
        const ProfileRecord record = track.getRecord(i);
        if (record.end < tMin) // i stays on this record: it was read, so the lapped check must cover it
            break;
        if (record.start <= tMax)
            out.push_back(record);
    }

    // Lapped check: everything we read must still be inside the ring NOW. i is the oldest index read.
    const uint64 cursorAfter = track.getCursor();
    const uint64 validLo = cursorAfter > ProfileTrack::CAPACITY ? cursorAfter - ProfileTrack::CAPACITY : 0;
    if (i < validLo)
    {
        out.clear();
        return false;
    }
    return true;
}
