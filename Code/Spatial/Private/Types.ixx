export module Spatial:Types;

import Core;
import Core.glm;
import Core.Frustum;
import :Morton;

export struct SpatialHandle
{
    uint32 idx = UINT32_MAX;
    uint32 gen = 0;

    bool isValid() const { return idx != UINT32_MAX; }
};

// RAII registration in the SpatialIndex, move-only like RenderNode/PhysicsBody. Adopts a
// handle from SpatialIndex::registerEntry and unregisters it on destruction.
export class SpatialEntry final
{
public:

    SpatialEntry() = default;
    explicit SpatialEntry(SpatialHandle handle) : m_handle(handle) {}
    SpatialEntry(const SpatialEntry&) = delete;
    SpatialEntry& operator=(const SpatialEntry&) = delete;
    SpatialEntry(SpatialEntry&& other) noexcept : m_handle(other.m_handle) { other.m_handle = {}; }
    SpatialEntry& operator=(SpatialEntry&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_handle = other.m_handle;
            other.m_handle = {};
        }
        return *this;
    }
    ~SpatialEntry() { reset(); }

    void reset(); // unregisters from Globals::spatialIndex, defined in spatialIndex.cpp

    bool isValid() const { return m_handle.isValid(); }
    SpatialHandle handle() const { return m_handle; }

private:

    SpatialHandle m_handle;
};

// Optional per-cell visibility test for hierarchical queries (CPU software occlusion, GPU Hi-Z
// readback, ...). Called after the frustum test passes; bounds are camera-relative floats.
export struct IOcclusionTester
{
    virtual bool isVisible(const glm::vec3& centerRelCamera, const glm::vec3& halfExtent) = 0;
};

export constexpr uint32 SpatialLayer_Render = 1u << 0; // entities with a RenderComponent
export constexpr uint32 SpatialLayer_Stress = 1u << 1; // synthetic stress-test entries

// Rebase a world-space frustum to camera-relative space (in double, so the planes stay exact at
// planet-scale camera positions): dot(n, p) + w == dot(n, p - camPos) + (w + dot(n, camPos)).
export inline Frustum rebaseFrustum(const Frustum& world, const glm::dvec3& cameraPos)
{
    Frustum out = world;
    for (uint32 i = 0; i < 6; ++i)
        out.planes[i].w = float(double(world.planes[i].w) + glm::dot(glm::dvec3(world.planes[i]), cameraPos));
    return out;
}

export inline void inflateFrustum(Frustum& frustum, float margin)
{
    for (uint32 i = 0; i < 6; ++i)
        frustum.planes[i].w += margin;
}

export enum class ESpatialCullMode : int
{
    Off = 0,     // markVisibleSet not run, everything renders
    StatsOnly,   // visibility query runs for its stats, nothing is gated
    SafeCull,    // gate on frustum ÃƒÂ¢Ã‹â€ Ã‚Âª camera ball, so nearby off-screen casters keep shadows/GI alive
    FrustumOnly, // gate on the view frustum alone ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â debug/measurement, breaks off-screen shadows by design
};

// Live-tweakable culling settings (Spatial/Culling): the App drives markVisibleSet from these and
// Entity::updateTree gates its render push on mode >= SafeCull.
export struct SpatialCullingConfig
{
    int mode = int(ESpatialCullMode::Off);
    bool freeze = false;             // stop re-stamping, fly around to inspect the culled set
    float margin = 4.0f;             // frustum inflation masking the one-frame stamp latency
    float safeRadius = 256.0f;       // SafeCull: everything within this range renders regardless
    float maxDist = 5000.0f;
    float skinnedRadiusScale = 1.5f; // animation can exceed the bind-pose bounds sphere
};

export struct SpatialIndexDesc
{
    uint32 numLevels = Morton::MaxLevels;
    uint32 initialCellCapacity = 4096;  // per level, rounded up to a power of two
    uint32 initialEntryCapacity = 4096;
};

export struct SpatialStats
{
    int numEntries = 0;
    int numCells = 0;
    int cellsTested = 0;      // per-frame query counters, reset each commitFrame
    int cellsFullyInside = 0;
    int entityTests = 0;
    int visibleCount = 0;
    int staticEntries = 0;
    int staticRebuilds = 0;
    float commitMs = 0.0f;
    float markVisibleMs = 0.0f;
    float rebuildMs = 0.0f; // duration of the most recent static-tier rebuild
    int perLevelCells[Morton::MaxLevels] = {};
    int perLevelEntities[Morton::MaxLevels] = {};
};
