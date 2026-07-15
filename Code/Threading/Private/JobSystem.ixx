export module Threading:JobSystem;

import Core;
import :Types;
import :MPMCQueue;
import :FreeStack;
import :StealDeque;

// A job executes on a fiber, so it can wait() mid-execution without blocking its worker thread:
// the fiber parks on the counter's wait list, the worker switches back to its scheduler loop and
// runs other jobs, and when the counter hits zero the fiber is pushed to the resume queue and
// continues on whichever worker picks it up (possibly a different thread - hence /GT fiber-safe
// TLS in the root CMakeLists; job code must not cache thread_local addresses across a wait).
struct Fiber
{
    enum class EState : uint32 { Idle, Running, Parked, Finished };

    void* handle = nullptr;      // Win32 fiber
    void* returnFiber = nullptr; // scheduler fiber of the worker currently running us
    Job* job = nullptr;
    EState state = EState::Idle;
    std::atomic<uint32> switchDone = 0;  // parked fiber has fully switched out; resumers spin on this
    std::atomic<uint32> debugRunning = 0; // debug-only tripwire: a fiber must never run on two workers
};

struct alignas(64) WorkerContext
{
    StealDeque deque;
    Fiber* currentFiber = nullptr;  // non-null while this worker is inside a job fiber
    void* schedulerFiber = nullptr;
    uint32 index = 0;
    uint32 stealSeed = 0;
    bool isWorker = false;          // false for the registered main thread (helps in wait, never parks)
    uint64 numExecuted = 0;
    uint64 numStolen = 0;
    uint64 numParked = 0;
    uint64 numResumed = 0;
    uint64 numSleeps = 0;
};

// Fiber-based work-stealing job scheduler. All memory is allocated in initialize(); the
// steady-state hot paths are lock-free (Chase-Lev deque per worker, Vyukov MPMC rings for the
// priority queues / fiber freelist / resume queue / job pool) and workers sleep on an eventcount
// (std::atomic wait -> WaitOnAddress) when idle. Priorities order the shared queues; a worker's
// own continuations always run LIFO from its local deque first, which is what makes fork/join
// fast. Delayed jobs sit in a min-heap serviced by a dedicated timer thread. initialize() must
// be called on the main thread: it registers the caller as a helper context so wait() on the
// main thread runs jobs instead of blocking.
export class JobSystem final
{
public:

    void initialize(const JobSystemDesc& desc = {});
    void shutdown(); // call from main before globals tear down; joins workers, drops leftovers

    bool isInitialized() const { return m_numContexts != 0; }
    uint32 getNumWorkers() const { return m_numWorkers; }
    JobSystemStats getStats() const;

    // Fire-and-forget or counter-tracked async execution. The callable must fit Job::StorageSize.
    // counter (optional) is incremented now and decremented when the job finishes: submit
    // everything, then wait once.
    template<typename Func>
    void submit(Func&& func, EJobPriority priority = EJobPriority::Normal, JobCounter* counter = nullptr, const char* name = nullptr)
    {
        Job* job = allocatePooledJob();
        if (!job) // pool exhausted: run it here rather than lose it
        {
            m_numInlineFallbacks.fetch_add(1, std::memory_order_relaxed);
            assert(false && "job pool exhausted - raise JobSystemDesc::jobPoolCapacity");
            func();
            return;
        }
        setJobCallable(*job, std::forward<Func>(func));
        job->priority = priority;
        job->name = name;
        if (counter)
        {
            counter->add(1);
            job->signal = counter;
        }
        submitReady(job);
    }

    template<typename Func>
    void submitDelayed(double delaySec, Func&& func, EJobPriority priority = EJobPriority::Normal, JobCounter* counter = nullptr, const char* name = nullptr)
    {
        Job* job = allocatePooledJob();
        if (!job)
        {
            m_numInlineFallbacks.fetch_add(1, std::memory_order_relaxed);
            assert(false && "job pool exhausted - raise JobSystemDesc::jobPoolCapacity");
            func();
            return;
        }
        setJobCallable(*job, std::forward<Func>(func));
        job->priority = priority;
        job->name = name;
        if (counter)
        {
            counter->add(1);
            job->signal = counter;
        }
        queueTimed(job, delaySec);
    }

    // Waits until the counter reaches zero. On a job fiber this parks the fiber (the worker keeps
    // running other jobs); on the main thread it runs jobs while waiting; on any other thread it
    // blocks on the atomic.
    void wait(JobCounter& counter);

    // Registered non-worker threads (main) can pump one ready job manually, e.g. to burn a stall.
    bool tryRunOneJob();

    // Splits [begin, end) into grainSize chunks pulled from a shared atomic cursor by
    // getNumWorkers() helper jobs plus the calling thread. func(chunkBegin, chunkEnd) must be
    // safe to run concurrently with itself. Returns when the whole range is done.
    template<typename Func>
    void parallelFor(uint32 begin, uint32 end, uint32 grainSize, Func&& func, EJobPriority priority = EJobPriority::Normal)
    {
        if (end <= begin)
            return;
        const uint32 count = end - begin;
        grainSize = std::max(grainSize, 1u);
        if (count <= grainSize || m_numWorkers == 0)
        {
            func(begin, end);
            return;
        }
        struct Shared
        {
            std::atomic<uint64> next;
            uint64 end;
            uint32 grain;
            std::remove_reference_t<Func>* func;
        };
        Shared shared{ begin, end, grainSize, &func };
        auto runner = [&shared]()
        {
            for (;;)
            {
                const uint64 chunkBegin = shared.next.fetch_add(shared.grain, std::memory_order_relaxed);
                if (chunkBegin >= shared.end)
                    return;
                (*shared.func)(uint32(chunkBegin), uint32(std::min(chunkBegin + shared.grain, shared.end)));
            }
        };
        const uint32 numChunks = (count + grainSize - 1) / grainSize;
        const uint32 numHelpers = std::min(m_numWorkers, numChunks - 1);
        JobCounter counter;
        for (uint32 i = 0; i < numHelpers; ++i)
            submit(runner, priority, &counter, "parallelFor");
        runner();
        wait(counter);
    }

    // Scheduler plumbing shared with JobGraph - not for gameplay code.
    Job* allocatePooledJob();
    void submitReady(Job* job);                        // job->pending must already be zero
    void submitReadyBatch(std::span<Job* const> jobs); // fan-out path: straight to the shared queues

private:

    friend class JobGraph;
    static void __stdcall fiberEntry(void* param);

    void workerMain(uint32 contextIndex);
    void timerMain();
    void fiberMain(Fiber& fiber);
    void runFiber(WorkerContext& ctx, Fiber* fiber);
    void execute(Job& job);
    Job* getWork(WorkerContext& ctx);
    Job* trySteal(WorkerContext& ctx);
    void pushReadyJob(Job* job);
    void idle(WorkerContext& ctx);
    bool anyWorkForWorker() const;
    void wakeMany(uint32 count);
    void wakeOne() { wakeMany(1); }
    void signalCounter(JobCounter& counter);
    void fiberWait(JobCounter& counter, WorkerContext& ctx);
    void helpWait(JobCounter& counter, WorkerContext& ctx);
    void releasePooledJob(Job* job);
    void queueTimed(Job* job, double delaySec);
    void dropJob(Job* job); // shutdown path: destroy the capture, release counters/pool slot

    struct TimedJob
    {
        Clock::time_point due;
        Job* job;
    };
    struct TimedJobLater
    {
        bool operator()(const TimedJob& a, const TimedJob& b) const { return a.due > b.due; }
    };

    std::unique_ptr<Job[]> m_jobPool;
    std::unique_ptr<Fiber[]> m_fibers;
    std::unique_ptr<WorkerContext[]> m_contexts; // [0] = main thread, [1..numWorkers] = workers
    std::vector<std::thread> m_threads;
    TaggedIndexStack m_freeJobs;
    TaggedIndexStack m_freeFibers;
    MPMCQueue<Fiber*> m_resumeQueue;
    MPMCQueue<Job*> m_readyQueues[NumJobPriorities];
    uint32 m_numWorkers = 0;
    uint32 m_numContexts = 0;
    uint32 m_numFibers = 0;
    uint32 m_jobPoolCapacity = 0;
    std::atomic<bool> m_running = false;
    std::atomic<uint64> m_numInlineFallbacks = 0;
    std::atomic<int32> m_pooledInFlight = 0;

    alignas(64) std::atomic<uint32> m_wakeEpoch = 0;
    std::atomic<uint32> m_numSleepers = 0;

    std::thread m_timerThread;
    std::mutex m_timedMutex;
    std::condition_variable m_timedCv;
    std::priority_queue<TimedJob, std::vector<TimedJob>, TimedJobLater> m_timedJobs;
};

export namespace Globals
{
    JobSystem jobSystem;
}
