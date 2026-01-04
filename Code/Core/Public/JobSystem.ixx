export module Core.JobSystem;
extern "C++" {

import Core;
import Core.LockFreeList;
import Core.Allocator;

struct promise_type;
export class JobScheduler;

export class Job final
{
public:
    using promise_type = promise_type;

    Job() : m_pPromise(nullptr) {}
    Job(promise_type* pPromise) : m_pPromise(pPromise) {}
    Job(Job&& move) : m_pPromise(move.m_pPromise) { move.m_pPromise = nullptr; }
    Job(const Job&) = delete;

    promise_type& promise() { return *m_pPromise; }

    bool await_ready() noexcept;
    void await_suspend(std::coroutine_handle<promise_type> h) noexcept;
    void await_resume() noexcept {}

private:

    promise_type* m_pPromise;
};

struct promise_type : LockFreeList<promise_type>::Entry
{
    friend class Job;
    enum EState : int { EState_Unadded, EState_Added, EState_Finished, EState_Destroyed };

    JobScheduler* m_pScheduler = nullptr;
    const char* m_pName = nullptr;
    EState m_state = EState_Unadded;
    std::mutex m_mutex;
    std::list<promise_type*, Allocator::toStd<promise_type*>> m_dependantJobs;

    promise_type(const char*& pName, Allocator& alloc = Globals::allocator) : m_pName(pName), m_dependantJobs(alloc) {}
    promise_type(const auto& self, const char*& pName, Allocator& alloc = Globals::allocator) : m_pName(pName), m_dependantJobs(alloc) {}
    promise_type(const promise_type& copy) = delete;

    Job get_return_object() { std::unique_lock lock(m_mutex); return Job(this); }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { m_state = EState_Finished; return {}; }
    void return_void() {}
    void unhandled_exception() { assert(false); }

    void* operator new(size_t size, const auto& self, const char*& pName, Allocator& alloc = g_heapAllocator)
    {
        return alloc.allocate(size);
    }

    void operator delete(void* p)
    {
        Allocator::globalFree(p);
    }
};

export class JobScheduler final
{
public:

    JobScheduler(int numThreads)
    {
        m_threads.reserve(numThreads);
        for (int i = 0; i < numThreads; ++i)
            m_threads.emplace_back([this]() { while (m_running) processJob(); });
    }
    ~JobScheduler() { stop(); }
    JobScheduler(const JobScheduler&) = delete;

    void processJob()
    {
        if (promise_type* promise = m_jobs.pop())
        {
            std::coroutine_handle<promise_type>::from_promise(*promise).resume();
            if (promise->m_state == promise_type::EState_Finished)
            {
                promise->m_mutex.lock();
                std::list<promise_type*, Allocator::toStd<promise_type*>> list = std::move(promise->m_dependantJobs);
                promise->m_mutex.unlock();
                m_jobs.push_list_front(list);
            }
            m_runningJobCount--;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    void add(Job& job)
    {
        auto& promise = job.promise();
        std::unique_lock lock(promise.m_mutex);
        if (promise.m_state == promise_type::EState_Unadded)
        {
            m_runningJobCount++;
            promise.m_pScheduler = this;
            promise.m_state = promise_type::EState_Added;
            m_jobs.push_front(promise);
        }
    }

    void stop()
    {
        while (!isEmpty())
            processJob();
        m_running = false;
        for (auto& thread : m_threads)
            thread.join();
    }

    bool isEmpty() { return m_jobs.empty() && m_runningJobCount == 0; }

private:

    void add(promise_type& promise, promise_type& depPromise)
    {
        switch (promise.m_state)
        {
        case promise_type::EState_Unadded:
            m_runningJobCount += 2;
            promise.m_state = promise_type::EState_Added;
            promise.m_dependantJobs.push_back(&depPromise);
            m_jobs.push_front(promise);
            break;
        case promise_type::EState_Added:
            m_runningJobCount++;
            promise.m_dependantJobs.push_back(&depPromise);
            break;
        case promise_type::EState_Finished:
            m_runningJobCount++;
            m_jobs.push_front(depPromise);
            break;
        default: assert(false);
        }
    }

private:
    friend class Job;

    bool m_running = true;
    std::vector<std::thread> m_threads;
    LockFreeList<promise_type> m_jobs;
    std::atomic<int> m_runningJobCount = 0;
};

bool Job::await_ready() noexcept
{
    return m_pPromise->m_state == promise_type::EState_Finished;
}

void Job::await_suspend(std::coroutine_handle<Job::promise_type> h) noexcept
{
    promise_type& prom = promise();
    promise_type& depProm = h.promise();
    JobScheduler* pScheduler = depProm.m_pScheduler;
    prom.m_pScheduler = pScheduler;
    std::unique_lock lock(prom.m_mutex);
    pScheduler->add(prom, depProm);
}
} // extern "C++"