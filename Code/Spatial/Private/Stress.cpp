module Spatial;

import Core;
import Core.glm;
import Core.Frustum;
import Core.Tweaks;

void SpatialStressTest::initialize()
{
    Tweak::intVar("Spatial/Stress", "Count", &m_count, 0, 10'000'000);
    Tweak::floatVar("Spatial/Stress", "Extent", &m_extent, 1.0f, 1'000'000.0f, 10.0f);
    Tweak::boolean("Spatial/Stress", "Spawn", &m_spawnRequested);
    Tweak::boolean("Spatial/Stress", "Clear", &m_clearRequested);
    Tweak::floatVar("Spatial/Stress", "Churn %", &m_churnPercent, 0.0f, 100.0f, 0.1f);
    Tweak::boolean("Spatial/Stress", "Sphere query", &m_runSphereQuery);
    Tweak::floatVar("Spatial/Stress", "Query radius", &m_queryRadius, 1.0f, 100'000.0f, 1.0f);
    Tweak::intVar("Spatial/Stress", "Query hits", &m_queryHits, 0, INT32_MAX);
    Tweak::floatVar("Spatial/Stress", "Query ms", &m_queryMs, 0.0f, FLT_MAX, 0.001f);
    Tweak::boolean("Spatial/Stress", "Verify brute force", &m_verifyBruteForce);
    Tweak::intVar("Spatial/Stress", "Verify delta", &m_verifyDelta, 0, INT32_MAX);
    Tweak::boolean("Spatial/Stress", "Frustum query", &m_runFrustumQuery);
    Tweak::floatVar("Spatial/Stress", "Frustum max dist", &m_frustumMaxDist, 1.0f, 1'000'000.0f, 10.0f);
    Tweak::intVar("Spatial/Stress", "Frustum hits", &m_frustumHits, 0, INT32_MAX);
    Tweak::floatVar("Spatial/Stress", "Frustum ms", &m_frustumMs, 0.0f, FLT_MAX, 0.001f);
}

void SpatialStressTest::update(const glm::dvec3& cameraPos, const Frustum& frustum)
{
    if (m_clearRequested)
    {
        m_clearRequested = false;
        clearEntries();
    }
    if (m_spawnRequested)
    {
        m_spawnRequested = false;
        clearEntries();
        spawnEntries();
    }
    if (m_churnPercent > 0.0f && !m_handles.empty())
        churn();

    if (m_runSphereQuery)
    {
        const auto start = Clock::now();
        m_queryHits = int(Globals::spatialIndex.querySphere(cameraPos, m_queryRadius, SpatialLayer_Stress, m_queryResults));
        m_queryMs = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
        if (m_verifyBruteForce)
        {
            // double-precision reference; tiny deltas are float rounding on the sphere boundary
            int expected = 0;
            for (size_t i = 0; i < m_positions.size(); ++i)
            {
                const double r = double(m_queryRadius) + double(m_radii[i]);
                const glm::dvec3 d = m_positions[i] - cameraPos;
                expected += glm::dot(d, d) <= r * r ? 1 : 0;
            }
            m_verifyDelta = glm::abs(expected - m_queryHits);
        }
    }
    else
    {
        m_queryHits = 0;
        m_queryMs = 0.0f;
    }

    if (m_runFrustumQuery)
    {
        const auto start = Clock::now();
        m_frustumHits = int(Globals::spatialIndex.queryFrustum(rebaseFrustum(frustum, cameraPos), cameraPos,
            m_frustumMaxDist, SpatialLayer_Stress, m_queryResults));
        m_frustumMs = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
    }
    else
    {
        m_frustumHits = 0;
        m_frustumMs = 0.0f;
    }
}

void SpatialStressTest::spawnEntries()
{
    m_handles.reserve(uint32(m_count));
    m_positions.reserve(uint32(m_count));
    m_radii.reserve(uint32(m_count));
    for (int i = 0; i < m_count; ++i)
    {
        const glm::dvec3 pos = glm::dvec3(randomSym(), randomSym(), randomSym()) * double(m_extent);
        const float radius = 0.25f * exp2f(randomUnit() * 6.0f); // log-uniform 0.25m .. 16m
        m_positions.push_back(pos);
        m_radii.push_back(radius);
        m_handles.push_back(Globals::spatialIndex.registerEntry(pos, radius, uint64(i), SpatialLayer_Stress));
    }
}

void SpatialStressTest::clearEntries()
{
    for (const SpatialHandle& handle : m_handles)
        Globals::spatialIndex.unregisterEntry(handle);
    m_handles.clear();
    m_positions.clear();
    m_radii.clear();
    m_churnCursor = 0;
}

void SpatialStressTest::churn()
{
    const uint32 total = uint32(m_handles.size());
    uint32 numToMove = uint32(double(total) * double(m_churnPercent) * 0.01);
    if (numToMove > total)
        numToMove = total;
    const double bound = double(m_extent);
    for (uint32 n = 0; n < numToMove; ++n)
    {
        const uint32 i = m_churnCursor++ % total;
        glm::dvec3& pos = m_positions[i];
        pos += glm::dvec3(randomSym(), randomSym(), randomSym()) * 0.5;
        pos = glm::clamp(pos, glm::dvec3(-bound), glm::dvec3(bound));
        Globals::spatialIndex.updateEntry(m_handles[i], pos, m_radii[i]);
    }
}
