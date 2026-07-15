module;

#include <intrin.h>

module Threading;

import Core;
import Core.Log;
import Core.Windows;

// FIBER_FLAG_FLOAT_SWITCH: a macro in winbase.h, doesn't cross the header-unit boundary.
static constexpr DWORD FiberFlagFloatSwitch = 0x1;

// The worker (or registered main-thread) context of the current thread. Fibers migrate between
// worker threads across a parked wait, so job code must re-read this after any wait - which is
// exactly what happens naturally as long as TLS isn't cached across the switch (/GT).
static thread_local WorkerContext* t_worker = nullptr;

// Nesting depth of inline job execution on a non-fiber thread (main helping in wait). A job run
// inline that waits helps again, possibly picking up another waiting job - without a cap that
// recursion grows the host stack unboundedly under sustained load. Beyond the cap the thread
// just waits; the workers guarantee progress.
static thread_local uint32 t_helpDepth = 0;
static constexpr uint32 MaxHelpDepth = 4;

// For the internal recycling rings (free fibers/jobs, resume queue) a push can transiently fail
// even though the ring is sized beyond its whole population: a preempted pop has claimed a cell
// but not republished its sequence yet. The popper is running, not blocked, so spinning is
// correct and bounded.
template<typename T>
static void pushMust(MPMCQueue<T>& queue, const T& value)
{
    while (!queue.push(value))
        _mm_pause();
}

void JobSystem::initialize(const JobSystemDesc& desc)
{
    assert(!isInitialized());
    const uint32 hardware = std::max(2u, std::thread::hardware_concurrency());
    m_numWorkers = desc.numWorkers ? desc.numWorkers : std::max(1u, hardware - 2);
    m_numContexts = m_numWorkers + 1;
    m_numFibers = desc.numFibers;
    m_jobPoolCapacity = std::bit_ceil(desc.jobPoolCapacity);

    m_jobPool = std::make_unique<Job[]>(m_jobPoolCapacity);
    m_freeJobs.initialize(m_jobPoolCapacity);
    for (uint32 i = 0; i < m_jobPoolCapacity; ++i)
        m_freeJobs.push(i);

    for (uint32 p = 0; p < NumJobPriorities; ++p)
        m_readyQueues[p].initialize(desc.queueCapacity);

    m_fibers = std::make_unique<Fiber[]>(m_numFibers);
    m_freeFibers.initialize(m_numFibers);
    m_resumeQueue.initialize(m_numFibers * 2); // 2x: a ring "full" can be transient, keep it unreachable
    for (uint32 i = 0; i < m_numFibers; ++i)
    {
        m_fibers[i].handle = CreateFiberEx(desc.fiberStackCommit, desc.fiberStackReserve, FiberFlagFloatSwitch, &JobSystem::fiberEntry, &m_fibers[i]);
        assert(m_fibers[i].handle);
        m_freeFibers.push(i);
    }

    m_contexts = std::make_unique<WorkerContext[]>(m_numContexts);
    for (uint32 i = 0; i < m_numContexts; ++i)
    {
        m_contexts[i].index = i;
        m_contexts[i].stealSeed = (0x9e3779b9u * (i + 1)) | 1u;
        m_contexts[i].isWorker = i != 0;
        m_contexts[i].deque.initialize(desc.dequeCapacity);
    }
    t_worker = &m_contexts[0]; // the calling (main) thread becomes the helper context

    m_running.store(true);
    m_threads.reserve(m_numWorkers);
    for (uint32 i = 1; i <= m_numWorkers; ++i)
    {
        std::thread& thread = m_threads.emplace_back(&JobSystem::workerMain, this, i);
        wchar_t threadName[24] = L"JobWorker";
        threadName[9] = wchar_t(L'0' + (i - 1) / 10);
        threadName[10] = wchar_t(L'0' + (i - 1) % 10);
        threadName[11] = 0;
        SetThreadDescription(HANDLE(thread.native_handle()), threadName);
    }
    m_timerThread = std::thread(&JobSystem::timerMain, this);
    SetThreadDescription(HANDLE(m_timerThread.native_handle()), L"JobTimer");

    char buf[128];
    sprintf_s(buf, "JobSystem: %u workers, %u fibers, %u pooled jobs", m_numWorkers, m_numFibers, m_jobPoolCapacity);
    Log::info(buf);
}

void JobSystem::shutdown()
{
    if (!isInitialized() || !m_running.exchange(false))
        return;
    {
        std::lock_guard lock(m_timedMutex); // a sleeping timer thread must observe m_running == false
    }
    m_timedCv.notify_all();
    m_wakeEpoch.fetch_add(1, std::memory_order_release);
    m_wakeEpoch.notify_all();
    for (std::thread& thread : m_threads)
        thread.join();
    m_timerThread.join();

    // nothing runs leftovers anymore - still release their captures/pool slots and unblock waiters
    for (uint32 p = 0; p < NumJobPriorities; ++p)
    {
        Job* job;
        while (m_readyQueues[p].pop(job))
            dropJob(job);
    }
    for (uint32 i = 0; i < m_numContexts; ++i)
        while (Job* job = m_contexts[i].deque.steal())
            dropJob(job);
    for (uint32 i = 0; i < m_numFibers; ++i)
    {
        assert(m_fibers[i].state != Fiber::EState::Parked && "fiber parked across shutdown - a wait never completed");
        DeleteFiber(m_fibers[i].handle);
    }
    m_threads.clear();
    m_fibers.reset();
    m_contexts.reset();
    m_jobPool.reset();
    m_numContexts = 0;
    m_numWorkers = 0;
    m_numFibers = 0;
    t_worker = nullptr;
    Log::info("JobSystem: shut down");
}

JobSystemStats JobSystem::getStats() const
{
    JobSystemStats stats;
    stats.numWorkers = m_numWorkers;
    stats.numFibers = m_numFibers;
    stats.numPooledInFlight = m_pooledInFlight.load(std::memory_order_relaxed);
    stats.numInlineFallbacks = m_numInlineFallbacks.load(std::memory_order_relaxed);
    for (uint32 i = 0; i < m_numContexts; ++i)
    {
        const WorkerContext& ctx = m_contexts[i];
        stats.numExecuted += ctx.numExecuted;
        stats.numStolen += ctx.numStolen;
        stats.numParked += ctx.numParked;
        stats.numResumed += ctx.numResumed;
        stats.numSleeps += ctx.numSleeps;
    }
    return stats;
}

void __stdcall JobSystem::fiberEntry(void* param)
{
    Globals::jobSystem.fiberMain(*static_cast<Fiber*>(param));
}

void JobSystem::fiberMain(Fiber& fiber)
{
    for (;;)
    {
        execute(*fiber.job);
        fiber.state = Fiber::EState::Finished;
        SwitchToFiber(fiber.returnFiber);
        // switched back in by runFiber with a fresh job
    }
}

void JobSystem::runFiber(WorkerContext& ctx, Fiber* fiber)
{
    assert(fiber->debugRunning.exchange(1, std::memory_order_acq_rel) == 0 && "fiber running on two workers");
    fiber->returnFiber = ctx.schedulerFiber;
    fiber->state = Fiber::EState::Running;
    ctx.currentFiber = fiber;
    SwitchToFiber(fiber->handle);
    ctx.currentFiber = nullptr;
    assert((fiber->debugRunning.store(0, std::memory_order_release), true));
    // read the state BEFORE publishing switchDone: the instant a parked fiber is resumable it can
    // be picked up, finish elsewhere, and have every field here rewritten for a new job
    const Fiber::EState state = fiber->state;
    if (state == Fiber::EState::Parked)
    {
        fiber->switchDone.store(1, std::memory_order_release);
    }
    else
    {
        assert(state == Fiber::EState::Finished);
        fiber->job = nullptr;
        m_freeFibers.push(uint32(fiber - m_fibers.get()));
        wakeOne(); // a worker that slept on fiber exhaustion can start jobs again
    }
}

void JobSystem::workerMain(uint32 contextIndex)
{
    WorkerContext& ctx = m_contexts[contextIndex];
    t_worker = &ctx;
    ctx.schedulerFiber = ConvertThreadToFiberEx(nullptr, FiberFlagFloatSwitch);
    for (;;)
    {
        Fiber* fiber;
        if (m_resumeQueue.pop(fiber)) // resumed waits first: they hold fibers and gate dependents
        {
            ctx.numResumed++;
            runFiber(ctx, fiber);
            continue;
        }
        if (!m_running.load(std::memory_order_relaxed))
            break;
        uint32 fiberIndex;
        if (m_freeFibers.pop(fiberIndex))
        {
            if (Job* job = getWork(ctx))
            {
                fiber = &m_fibers[fiberIndex];
                fiber->job = job;
                runFiber(ctx, fiber);
                continue;
            }
            m_freeFibers.push(fiberIndex);
        }
        idle(ctx);
    }
    ConvertFiberToThread();
    t_worker = nullptr;
}

void JobSystem::execute(Job& job)
{
    // pooled jobs never use their dependency counter - repurpose it as a double-run tripwire
    assert(!(job.flags & EJobFlag_Pooled) || job.pending.fetch_add(1, std::memory_order_acq_rel) == 0);
    if (WorkerContext* ctx = t_worker)
        ctx->numExecuted++;
    job.invoke(job.storage);
    Job* const* successors = job.successors;
    const uint32 numSuccessors = job.numSuccessors;
    JobCounter* signal = job.signal;
    if (job.flags & EJobFlag_Pooled)
    {
        if (job.destroy)
            job.destroy(job.storage);
        releasePooledJob(&job);
    }
    uint32 numReady = 0;
    for (uint32 i = 0; i < numSuccessors; ++i)
        if (successors[i]->pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            pushReadyJob(successors[i]);
            ++numReady;
        }
    if (numReady)
    {
        // re-read t_worker: the job body may have parked and resumed on a different thread. A
        // worker pops one successor itself right after this; wake helpers for the rest.
        WorkerContext* ctx = t_worker;
        const uint32 numToWake = (ctx && ctx->isWorker) ? numReady - 1 : numReady;
        if (numToWake)
            wakeMany(numToWake);
    }
    if (signal)
        signalCounter(*signal);
}

Job* JobSystem::getWork(WorkerContext& ctx)
{
    if (Job* job = ctx.deque.pop())
        return job;
    Job* job;
    for (uint32 p = 0; p < NumJobPriorities; ++p)
        if (m_readyQueues[p].pop(job))
            return job;
    return trySteal(ctx);
}

Job* JobSystem::trySteal(WorkerContext& ctx)
{
    uint32 seed = ctx.stealSeed;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    ctx.stealSeed = seed;
    const uint32 numContexts = m_numContexts;
    uint32 victim = seed % numContexts;
    for (uint32 i = 0; i < numContexts; ++i)
    {
        if (victim != ctx.index)
            if (Job* job = m_contexts[victim].deque.steal())
            {
                ctx.numStolen++;
                return job;
            }
        if (++victim == numContexts)
            victim = 0;
    }
    return nullptr;
}

void JobSystem::pushReadyJob(Job* job)
{
    WorkerContext* ctx = t_worker;
    if (ctx && ctx->isWorker && ctx->deque.push(job))
        return; // continuations run LIFO on the worker that made them ready
    if (m_readyQueues[uint32(job->priority)].push(job))
        return;
    // full (or transiently full behind a preempted popper): run it here rather than lose it
    m_numInlineFallbacks.fetch_add(1, std::memory_order_relaxed);
    execute(*job);
}

void JobSystem::submitReady(Job* job)
{
    assert(isInitialized());
    pushReadyJob(job);
    wakeOne();
}

void JobSystem::submitReadyBatch(std::span<Job* const> jobs)
{
    assert(isInitialized());
    for (Job* job : jobs)
    {
        // straight to the shared queues: batches are fan-out, a local deque would serialize the
        // start behind one steal per job
        if (!m_readyQueues[uint32(job->priority)].push(job))
        {
            m_numInlineFallbacks.fetch_add(1, std::memory_order_relaxed);
            execute(*job);
        }
    }
    wakeMany(uint32(jobs.size()));
}

Job* JobSystem::allocatePooledJob()
{
    uint32 index;
    if (!m_freeJobs.pop(index))
    {
        char buf[160];
        sprintf_s(buf, "JobSystem: pool exhausted, inFlight=%d executed=%llu",
            m_pooledInFlight.load(std::memory_order_relaxed), getStats().numExecuted);
        Log::error(buf);
        return nullptr;
    }
    m_pooledInFlight.fetch_add(1, std::memory_order_relaxed);
    Job* job = &m_jobPool[index];
    job->invoke = nullptr;
    job->destroy = nullptr;
    job->signal = nullptr;
    job->successors = nullptr;
    job->numSuccessors = 0;
    job->pending.store(0, std::memory_order_relaxed);
    job->initialPending = 0;
    job->priority = EJobPriority::Normal;
    job->flags = EJobFlag_Pooled;
    job->name = nullptr;
    return job;
}

void JobSystem::releasePooledJob(Job* job)
{
    m_pooledInFlight.fetch_sub(1, std::memory_order_relaxed);
    m_freeJobs.push(uint32(job - m_jobPool.get()));
}

void JobSystem::dropJob(Job* job)
{
    JobCounter* signal = job->signal;
    if (job->flags & EJobFlag_Pooled)
    {
        if (job->destroy)
            job->destroy(job->storage);
        releasePooledJob(job);
    }
    if (signal)
        signalCounter(*signal);
}

void JobSystem::idle(WorkerContext& ctx)
{
    // brief spin: catch work landing within ~a microsecond without a kernel round-trip
    for (uint32 spin = 0; spin < 8; ++spin)
    {
        for (uint32 pause = 0; pause < 8; ++pause)
            _mm_pause();
        if (anyWorkForWorker())
            return;
    }
    const uint32 epoch = m_wakeEpoch.load(std::memory_order_acquire);
    m_numSleepers.fetch_add(1, std::memory_order_seq_cst);
    // recheck AFTER announcing the sleep (Dekker with wakeMany): either we see the work here or
    // the submitter sees us and bumps the epoch
    if (anyWorkForWorker())
    {
        m_numSleepers.fetch_sub(1, std::memory_order_relaxed);
        return;
    }
    ctx.numSleeps++;
    m_wakeEpoch.wait(epoch, std::memory_order_acquire);
    m_numSleepers.fetch_sub(1, std::memory_order_relaxed);
}

bool JobSystem::anyWorkForWorker() const
{
    if (!m_running.load(std::memory_order_relaxed))
        return true; // wake to exit
    if (!m_resumeQueue.wasEmpty())
        return true;
    bool anyJobs = false;
    for (uint32 p = 0; p < NumJobPriorities && !anyJobs; ++p)
        anyJobs = !m_readyQueues[p].wasEmpty();
    for (uint32 i = 0; i < m_numContexts && !anyJobs; ++i)
        anyJobs = m_contexts[i].deque.maybeNonEmpty();
    if (!anyJobs)
        return false;
    return !m_freeFibers.wasEmpty(); // jobs without a free fiber can't start; the fiber release wakes us
}

void JobSystem::wakeMany(uint32 count)
{
    // pairs with the sleeper's seq_cst announce + recheck: at least one side sees the other
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const uint32 sleepers = m_numSleepers.load(std::memory_order_relaxed);
    if (!sleepers)
        return;
    m_wakeEpoch.fetch_add(1, std::memory_order_release);
    if (count >= sleepers)
        m_wakeEpoch.notify_all();
    else
        for (uint32 i = 0; i < count; ++i)
            m_wakeEpoch.notify_one();
}

void JobSystem::wait(JobCounter& counter)
{
    if (counter.isDone())
        return;
    WorkerContext* ctx = t_worker;
    if (ctx && ctx->currentFiber)
    {
        fiberWait(counter, *ctx);
        return;
    }
    if (ctx)
    {
        helpWait(counter, *ctx);
        return;
    }
    // unregistered external thread: block on the atomic (notified on the zero transition)
    uint32 count;
    while ((count = counter.m_count.load(std::memory_order_acquire)) != 0)
        counter.m_count.wait(count, std::memory_order_acquire);
}

bool JobSystem::tryRunOneJob()
{
    WorkerContext* ctx = t_worker;
    if (!ctx || ctx->currentFiber)
        return false;
    Job* job = getWork(*ctx);
    if (!job)
        return false;
    ++t_helpDepth;
    execute(*job);
    --t_helpDepth;
    return true;
}

void JobSystem::helpWait(JobCounter& counter, WorkerContext& ctx)
{
    const bool mayExecute = t_helpDepth < MaxHelpDepth;
    uint32 spins = 0;
    while (!counter.isDone())
    {
        if (mayExecute)
            if (Job* job = getWork(ctx))
            {
                ++t_helpDepth;
                execute(*job);
                --t_helpDepth;
                spins = 0;
                continue;
            }
        if (++spins < 64)
        {
            _mm_pause();
        }
        else
        {
            std::this_thread::yield();
            spins = 0;
        }
    }
}

void JobSystem::fiberWait(JobCounter& counter, WorkerContext& ctx)
{
    // announce the waiter inside the same atomic as the count: the zero transition either sees
    // the registration in its own fetch_sub (and claims the wait list, where it finds our node
    // if we managed to push one) or never touches counter memory again. Our registration bit
    // keeps the full value nonzero - nobody can observe completion and free the counter - until
    // whoever owns it (the transition for pushed nodes, us when backing out) removes it.
    const uint32 registered = counter.m_count.fetch_add(JobCounter::WaiterInc, std::memory_order_acq_rel);
    assert(registered < 0xff000000u);
    Fiber* fiber = ctx.currentFiber;
    FiberWaitNode node{ fiber, nullptr };
    bool pushed = false;
    if ((registered & JobCounter::CountMask) != 0)
    {
        fiber->switchDone.store(0, std::memory_order_relaxed); // ordered before the push's release
        fiber->state = Fiber::EState::Parked;
        FiberWaitNode* head = counter.m_waiterHead.load(std::memory_order_acquire);
        while (head != JobCounter::takenSentinel())
        {
            node.next = head;
            if (counter.m_waiterHead.compare_exchange_weak(head, &node, std::memory_order_release, std::memory_order_acquire))
            {
                pushed = true;
                break;
            }
        }
    }
    if (!pushed)
    {
        // the count already hit zero, or the transition claimed the list before our push landed
        // (it saw a taken sentinel). Either way it will never see our node - take the
        // registration back out ourselves, waking anyone who saw its transient nonzero value.
        fiber->state = Fiber::EState::Running;
        if (counter.m_count.fetch_sub(JobCounter::WaiterInc, std::memory_order_acq_rel) == JobCounter::WaiterInc)
            counter.m_count.notify_all();
        return;
    }
    ctx.numParked++;
    SwitchToFiber(fiber->returnFiber);
    // resumed - possibly on a different worker thread - after the counter reached zero
}

void JobSystem::signalCounter(JobCounter& counter)
{
    const uint32 old = counter.m_count.fetch_sub(1, std::memory_order_acq_rel);
    assert((old & JobCounter::CountMask) != 0);
    if ((old & JobCounter::CountMask) != 1)
        return;
    if (old == 1)
    {
        // reached zero with no registered waiters: the fetch_sub above was our last touch of
        // counter MEMORY - a poller/blocked waiter observing zero may free it right now.
        // notify_all is address-only (WakeByAddress keys on the address, never dereferences).
        counter.m_count.notify_all();
        return;
    }
    // reached zero with registrations outstanding: their bits keep the full value nonzero, so
    // the counter stays alive at least until our fetch_sub below. Claim the whole wait list with
    // one exchange; registrants who see the sentinel back their own bit out (strictly after this
    // exchange, so the head access is covered too).
    FiberWaitNode* waiters = counter.m_waiterHead.exchange(JobCounter::takenSentinel(), std::memory_order_acq_rel);
    assert(waiters != JobCounter::takenSentinel());
    uint32 numTaken = 0;
    for (FiberWaitNode* node = waiters; node; node = node->next)
        ++numTaken; // nodes live on parked fiber stacks, valid until we resume them below
    if (numTaken)
        counter.m_count.fetch_sub(numTaken * JobCounter::WaiterInc, std::memory_order_acq_rel); // last memory access
    counter.m_count.notify_all();
    uint32 numResumed = 0;
    while (waiters)
    {
        Fiber* fiber = waiters->fiber;
        waiters = waiters->next; // the node dies with the fiber's resume - read next first
        while (fiber->switchDone.load(std::memory_order_acquire) == 0)
            _mm_pause(); // the fiber is still switching out on its old worker
        pushMust(m_resumeQueue, fiber);
        ++numResumed;
    }
    if (numResumed)
        wakeMany(numResumed);
}

void JobSystem::queueTimed(Job* job, double delaySec)
{
    const Clock::time_point due = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(std::max(delaySec, 0.0)));
    {
        std::lock_guard lock(m_timedMutex);
        m_timedJobs.push({ due, job });
    }
    m_timedCv.notify_one();
}

void JobSystem::timerMain()
{
    std::unique_lock lock(m_timedMutex);
    while (m_running.load(std::memory_order_relaxed))
    {
        if (m_timedJobs.empty())
        {
            m_timedCv.wait(lock);
            continue;
        }
        const Clock::time_point due = m_timedJobs.top().due;
        if (Clock::now() < due)
        {
            m_timedCv.wait_until(lock, due);
            continue;
        }
        Job* job = m_timedJobs.top().job;
        m_timedJobs.pop();
        lock.unlock();
        submitReady(job);
        lock.lock();
    }
    // dropped delayed jobs still release their captures/pool slots and unblock waiters
    while (!m_timedJobs.empty())
    {
        Job* job = m_timedJobs.top().job;
        m_timedJobs.pop();
        lock.unlock();
        dropJob(job);
        lock.lock();
    }
}
