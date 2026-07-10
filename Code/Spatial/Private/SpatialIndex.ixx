export module Spatial:SpatialIndex;

import Core;
import Core.glm;
import Core.Frustum;
import :Morton;
import :Types;
import :CellMap;
import :RecordPool;
import :StaticStore;

// Spatial index independent of the entity parent/child hierarchy: an implicit 64-ary hierarchy
// over per-level hashed grids. Entries live at exactly one level (the one matching their bounding
// radius); every cell knows which of its 4x4x4 children are occupied, so queries descend the
// hierarchy with bit scans and never visit empty space. Mutations that change a cell are queued
// and applied in commitFrame(); same-cell position updates write in place. Queries are read-only
// and valid between commits.
export class SpatialIndex final
{
public:

    void initialize(const SpatialIndexDesc& desc = {});

    // spawnVisible: whether the entry counts as visible in every pass until its first real stamp.
    // Entities want true (a fresh spawn must not flash invisible during its link+stamp latency);
    // streamed geometry (terrain chunks) passes false — appearing one frame late is invisible for
    // something that didn't exist before, while the guard would leak never-stamped off-screen entries
    // into the main pass (permanently while the culling is frozen).
    SpatialHandle registerEntry(const glm::dvec3& pos, float radius, uint64 userData, uint32 layerMask = 1, bool spawnVisible = true);
    void unregisterEntry(SpatialHandle handle); // neutralized immediately, unlinked at commit
    void updateEntry(SpatialHandle handle, const glm::dvec3& pos, float radius);
    void setLayerMask(SpatialHandle handle, uint32 layerMask);
    void commitFrame();

    uint32 querySphere(const glm::dvec3& center, float radius, uint32 layerMask, std::vector<uint64>& outUserData) const;
    uint32 queryAABB(const glm::dvec3& boxMin, const glm::dvec3& boxMax, uint32 layerMask, std::vector<uint64>& outUserData) const;
    uint32 queryFrustum(const Frustum& frustumRelCamera, const glm::dvec3& cameraPos, float maxDist, uint32 layerMask,
                        std::vector<uint64>& outUserData, IOcclusionTester* occlusion = nullptr) const;
    uint32 queryRay(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist, uint32 layerMask,
                    std::vector<uint64>& outUserData) const; // broadphase: entries whose bounds cross the segment
    uint64 queryNearest(const glm::dvec3& pos, float maxRadius, uint32 layerMask, uint64 excludeUserData = 0) const;

    // Per-pass visibility stamps consumed by the render gate; each call invalidates that pass's
    // previous stamp generation (single consumer per pass by design). Main is the camera frustum
    // (occlusion-testable); Near is the camera ball that keeps off-screen shadow casters and
    // ray-traced geometry alive.
    void markVisibleSet(ESpatialPass pass, const Frustum& frustumRelCamera, const glm::dvec3& cameraPos, float maxDist,
                        uint32 layerMask, IOcclusionTester* occlusion = nullptr);
    void markVisibleSphere(ESpatialPass pass, const glm::dvec3& center, float radius, uint32 layerMask);

    // Main pass; a never-stamped entry counts as visible unless it registered with spawnVisible = false
    // (see registerEntry).
    bool isVisible(SpatialHandle handle) const
    {
        if (!m_pool.isValidAlive(handle))
            return false;
        const uint32 stamp = m_pool.lastVisible[uint32(ESpatialPass::Main)][handle.idx];
        return stamp == m_visibleQueryId[uint32(ESpatialPass::Main)]
            || (stamp == 0 && !(m_pool.flags[handle.idx] & RecordFlag_NoSpawnGuard));
    }

    uint32 getPassMask(SpatialHandle handle) const // SpatialPassBit_* bits; spawn guard as in isVisible
    {
        if (!m_pool.isValidAlive(handle))
            return 0;
        const bool spawnGuard = !(m_pool.flags[handle.idx] & RecordFlag_NoSpawnGuard);
        uint32 mask = 0;
        for (uint32 p = 0; p < uint32(ESpatialPass::Count); ++p)
        {
            const uint32 stamp = m_pool.lastVisible[p][handle.idx];
            if (stamp == m_visibleQueryId[p] || (spawnGuard && stamp == 0))
                mask |= 1u << p;
        }
        return mask;
    }

    glm::dvec3 getPosition(SpatialHandle handle) const;
    float getRadius(SpatialHandle handle) const;

    const SpatialStats& getStats() const { return m_stats; }
    const SpatialCullingConfig& getCullingConfig() const { return m_culling; }

private:

    struct PendingOp
    {
        enum EType : uint8 { Link, Move, Unlink };
        uint64 newKey;
        glm::vec3 newRelPos;
        float newRadius;
        uint32 idx;
        uint32 gen;
        EType type;
        uint8 newLevel;
    };

    struct EmptyCandidate
    {
        uint64 key;
        uint32 level;
    };

    void linkIntoCell(uint32 idx);
    void unlinkFromCell(uint32 idx);
    void setOccupancyBits(uint32 level, uint64 key);
    void demoteOrUnlink(uint32 idx); // pulls a StaticTier entry back to dynamic, else plain unlink
    void sweepEmptyCells();
    void promotionScan();
    void rebuildStaticLevel(uint32 level);

    template <typename Tester, typename EmitFunc>
    void traverse(const Tester& tester, const glm::dvec3& refPos, uint32 layerMask, const EmitFunc& emit) const;

    template <typename Tester, typename EmitFunc>
    void traverseCell(const Tester& tester, const glm::dvec3& refPos, uint32 layerMask,
                      uint64 key, uint32 level, const CellRecord& rec, bool fullyInside, const EmitFunc& emit) const;

    std::array<CellMap, Morton::MaxLevels> m_levels;
    std::array<StaticStore, Morton::MaxLevels> m_static;
    RecordPool m_pool;
    std::vector<PendingOp> m_pendingOps;
    std::vector<EmptyCandidate> m_emptyCandidates;
    uint32 m_levelEntityCount[Morton::MaxLevels] = {};
    uint32 m_numLevels = Morton::MaxLevels;
    uint32 m_frameId = 1;
    uint32 m_visibleQueryId[uint32(ESpatialPass::Count)] = {}; // stamp generation per pass, 0 = never stamped
    uint32 m_promoteCursor = 0;  // round-robin pool scan position for static promotion
    bool m_staticEnabled = true;
    int m_promoteAfterFrames = 60;
    int m_staticScanBudget = 65536;  // pool slots inspected per commit
    int m_staticRebuildBatch = 1024; // pending promotions that force a level rebuild
    float m_topLevelMaxRadius = 0.0f; // largest clamped-oversize radius, inflates top-level tests
    SpatialCullingConfig m_culling;
    mutable SpatialStats m_stats;
    bool m_initialized = false;
};

export namespace Globals
{
    SpatialIndex spatialIndex;
}
