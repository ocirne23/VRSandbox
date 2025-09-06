module Core.Time;

import <queue>;

import Core;

struct TimerCompare
{
    bool operator()(const Timer* lhs, const Timer* rhs) const
    {
        return *lhs > *rhs;
    }
};

static std::unordered_map<uint64, std::vector<Timer*>> m_timerBuckets;
static uint64 m_lastCheckedBucket = 0;

void Time::addTimer(Timer* pTimer)
{
    const auto currentTime = Globals::time.getCurrentTime();
    if (pTimer->m_endTime <= currentTime)
    {
        if (pTimer->m_onTimer)
        {
            if (pTimer->m_onTimer(*pTimer) == Timer::REPEAT)
            {
                pTimer->m_endTime = currentTime + (pTimer->m_endTime - pTimer->m_startTime);
                pTimer->m_startTime = currentTime;
                assert(pTimer->m_startTime != pTimer->m_endTime);
                addTimer(pTimer);
                return;
            }
        }
        pTimer->m_hasTriggered = true;
        return;
    }

    const uint64 bucket = (uint64)std::chrono::duration_cast<std::chrono::seconds>(pTimer->m_endTime.time_since_epoch()).count();
    if (m_lastCheckedBucket == 0)
        m_lastCheckedBucket = bucket - 1;

    m_timerBuckets[bucket].push_back(pTimer);
    std::push_heap(m_timerBuckets[bucket].begin(), m_timerBuckets[bucket].end(), TimerCompare());
    pTimer->m_hasTriggered = false;
}

void Time::removeTimer(Timer* pTimer) 
{
    const uint64 bucket = (uint64)std::chrono::duration_cast<std::chrono::seconds>(pTimer->m_endTime.time_since_epoch()).count();
    auto it = m_timerBuckets.find(bucket);
    if (it != m_timerBuckets.end())
    {
        std::vector<Timer*> timers = it->second;
        // swap and pop
        auto itTimer = std::find(timers.begin(), timers.end(), pTimer);
        if (itTimer != timers.end())
        {
            std::swap(*itTimer, timers.back());
            timers.pop_back();
            std::make_heap(timers.begin(), timers.end(), TimerCompare());
            if (timers.empty())
            {
                m_timerBuckets.erase(it);
            }
        }
    }
}

void Time::processTimers()
{
    const auto currentTime = Globals::time.getCurrentTime();
    const uint64 currentBucket = (uint64)std::chrono::duration_cast<std::chrono::seconds>(currentTime.time_since_epoch()).count();
    for (uint64 bucket = m_lastCheckedBucket; bucket <= currentBucket; ++bucket)
    {
        auto it = m_timerBuckets.find(bucket);
        if (it != m_timerBuckets.end())
        {
            auto& timers = it->second;
            while (!timers.empty())
            {
                Timer* pTimer = timers.front();
                if (pTimer->m_endTime <= currentTime)
                {
                    std::pop_heap(timers.begin(), timers.end(), TimerCompare());
                    timers.pop_back();
                    if (pTimer->m_onTimer)
                    {
                        if (pTimer->m_onTimer(*pTimer) == Timer::REPEAT)
                        {
                            pTimer->m_endTime = currentTime + (pTimer->m_endTime - pTimer->m_startTime);
                            pTimer->m_startTime = currentTime;
                            assert(pTimer->m_startTime < pTimer->m_endTime);
                            addTimer(pTimer);
                        }
                    }
                    pTimer->m_hasTriggered = true;
                    if (timers.empty())
                    {
                        m_timerBuckets.erase(it);
                    }
                }
                else
                {
                    break;
                }
            }
        }
        m_lastCheckedBucket = currentBucket;
    }
}