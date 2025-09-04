export module Core.TimeManager;

import <queue>;

import Core;

export class TimeManager
{
public:

    void update()
    {
        m_currentTime = Clock::now();
        m_deltaSec = std::chrono::duration<double>(m_currentTime - m_lastTime).count();
        m_elapsedSec = std::chrono::duration<double>(m_currentTime - m_startTime).count();
        m_lastTime = m_currentTime;

        //const uint64 msSinceStart = std::chrono::duration_cast<std::chrono::milliseconds>(m_currentTime - m_startTime).count();
    }

    double getDeltaSec() const
    {
        return m_deltaSec;
    }

    double getElapsedSec() const
    {
        return m_elapsedSec;
    }

    Clock::time_point getCurrentTime() const
    {
        return m_currentTime;
    }

private:

    const Clock::time_point m_startTime = Clock::now();
    Clock::time_point m_lastTime = Clock::now();
    Clock::time_point m_currentTime = Clock::now();
    double m_deltaSec = 0.0;
    double m_elapsedSec = 0.0;

    std::unordered_map<uint64, std::priority_queue<class Timer*>> m_timerBuckets;
    uint64 m_lastCheckedBucket = 0;
};

export namespace Globals
{
    TimeManager time;
}

export class Timer
{
public:

    Timer(Clock::duration duration = Clock::duration::min()) : m_duration(duration)
    {
        m_startTime = Globals::time.getCurrentTime();
    }

    ~Timer()
    {
    }

    Timer(const Timer&) = delete;
    Timer(const Timer&&) = delete;

    double getElapsedSec() const
    {
        return std::chrono::duration<double>(Globals::time.getCurrentTime() - m_startTime).count();
    }

    void reset(Clock::duration duration = Clock::duration::min())
    {
        m_startTime = Globals::time.getCurrentTime();
        m_duration = duration;
    }

private:

    friend class TimeManager;
    Clock::time_point m_startTime;
    Clock::duration m_duration;
};