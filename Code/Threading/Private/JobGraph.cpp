module Threading;

import Core;

JobGraphBuilder& JobGraphBuilder::reads(const JobResource& resource)
{
    m_graph->declareRead(m_index, resource.id());
    return *this;
}

JobGraphBuilder& JobGraphBuilder::writes(const JobResource& resource)
{
    m_graph->declareWrite(m_index, resource.id());
    return *this;
}

JobGraphBuilder& JobGraphBuilder::after(JobId predecessor)
{
    assert(predecessor.index < m_index && "after() only orders behind previously declared jobs");
    m_graph->addEdge(predecessor.index, m_index);
    return *this;
}

JobGraphBuilder& JobGraphBuilder::priority(EJobPriority priority)
{
    m_graph->jobAt(m_index).priority = priority;
    return *this;
}

Job& JobGraph::allocateJob()
{
    if (m_numJobs == uint32(m_chunks.size()) * JobsPerChunk)
        m_chunks.push_back(std::make_unique<Job[]>(JobsPerChunk));
    return jobAt(m_numJobs++);
}

void JobGraph::declareRead(uint32 jobIndex, uint32 resourceId)
{
    if (resourceId >= m_resources.size())
        m_resources.resize(resourceId + 1);
    ResourceState& resource = m_resources[resourceId];
    if (resource.lastWriter >= 0)
        addEdge(uint32(resource.lastWriter), jobIndex);
    resource.readersSinceWrite.push_back(jobIndex);
}

void JobGraph::declareWrite(uint32 jobIndex, uint32 resourceId)
{
    if (resourceId >= m_resources.size())
        m_resources.resize(resourceId + 1);
    ResourceState& resource = m_resources[resourceId];
    if (!resource.readersSinceWrite.empty())
    {
        // the readers already depend on the previous writer, so these edges cover write-after-write too
        for (uint32 reader : resource.readersSinceWrite)
            addEdge(reader, jobIndex);
        resource.readersSinceWrite.clear();
    }
    else if (resource.lastWriter >= 0)
    {
        addEdge(uint32(resource.lastWriter), jobIndex);
    }
    resource.lastWriter = int32(jobIndex);
}

void JobGraph::addEdge(uint32 from, uint32 to)
{
    if (from == to) // a job that reads and writes the same resource depends only on others
        return;
    assert(from < to);
    m_compiled = false;
    m_edges.emplace_back(from, to);
}

void JobGraph::compile()
{
    assert(!isRunning());
    std::sort(m_edges.begin(), m_edges.end());
    m_edges.erase(std::unique(m_edges.begin(), m_edges.end()), m_edges.end());

    for (uint32 i = 0; i < m_numJobs; ++i)
    {
        Job& job = jobAt(i);
        job.initialPending = 0;
        job.numSuccessors = 0;
    }
    for (const auto& [from, to] : m_edges)
    {
        jobAt(to).initialPending++;
        jobAt(from).numSuccessors++;
    }

    m_successorStorage.resize(m_edges.size());
    uint32 edge = 0;
    uint32 offset = 0;
    m_roots.clear();
    for (uint32 i = 0; i < m_numJobs; ++i)
    {
        Job& job = jobAt(i);
        job.successors = m_successorStorage.data() + offset;
        for (; edge < uint32(m_edges.size()) && m_edges[edge].first == i; ++edge)
            m_successorStorage[offset++] = &jobAt(m_edges[edge].second);
        if (job.initialPending == 0)
            m_roots.push_back(&job);
    }
    assert(edge == uint32(m_edges.size()));
    m_compiled = true;
}

void JobGraph::run()
{
    assert(m_compiled && !isRunning());
    if (m_numJobs == 0)
        return;
    m_counter.add(m_numJobs);
    for (uint32 i = 0; i < m_numJobs; ++i)
    {
        Job& job = jobAt(i);
        job.pending.store(int32(job.initialPending), std::memory_order_relaxed);
    }
    // the queue pushes release these resets to whoever pops a root; successors follow transitively
    std::atomic_thread_fence(std::memory_order_release);
    Globals::jobSystem.submitReadyBatch(m_roots);
}

void JobGraph::clear()
{
    assert(!isRunning());
    for (uint32 i = 0; i < m_numJobs; ++i)
    {
        Job& job = jobAt(i);
        if (job.destroy)
            job.destroy(job.storage);
        job.invoke = nullptr;
        job.destroy = nullptr;
        job.signal = nullptr;
        job.successors = nullptr;
        job.numSuccessors = 0;
    }
    m_numJobs = 0;
    m_edges.clear();
    m_resources.clear();
    m_successorStorage.clear();
    m_roots.clear();
    m_compiled = false;
}
