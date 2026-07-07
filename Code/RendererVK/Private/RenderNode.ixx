export module RendererVK:RenderNode;

import Core;
import Core.glm;
import Core.Sphere;
import Core.Transform;

import :Layout;

export namespace Globals
{
    std::vector<Transform> renderNodeTransforms;

    // Sparse transform-upload tracking: per slot, one pending-upload bit per frame in flight, plus
    // the per-frame index lists Renderer::present drains (only changed slots are copied into that
    // frame's GPU buffer instead of the whole array). Maintained by markRenderNodeTransformDirty.
    std::vector<uint8> renderNodeDirtyBits;
    std::array<std::vector<uint32>, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> renderNodeDirtyLists;
}

export inline void markRenderNodeTransformDirty(uint32 idx)
{
    constexpr uint8 allBits = uint8((1u << RendererVKLayout::NUM_FRAMES_IN_FLIGHT) - 1);
    uint8& bits = Globals::renderNodeDirtyBits[idx];
    if (bits == allBits)
        return;
    for (uint32 frame = 0; frame < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++frame)
        if (!(bits & (1u << frame)))
            Globals::renderNodeDirtyLists[frame].push_back(idx);
    bits = allBits;
}

// RAII handle to a spawned renderer instance (movable, like PhysicsBody). Destroying the handle
// recycles its renderer resources: the transform slot goes on a free list, and a skinned node's whole
// allocation bundle is parked for reuse by the next spawnSkinnedNode of the same container.
export class RenderNode final
{
public:

    RenderNode() = default;
    RenderNode(const RenderNode&) = delete;
    RenderNode& operator=(const RenderNode&) = delete;
    RenderNode(RenderNode&& other) noexcept { moveFrom(std::move(other)); }
    RenderNode& operator=(RenderNode&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            moveFrom(std::move(other));
        }
        return *this;
    }
    ~RenderNode() { destroy(); }

    bool isValid() const { return m_transformIdx != UINT32_MAX; }
    void destroy();

    // The only write path: unchanged transforms cost one compare, changed ones are queued for
    // sparse upload (writing through a mutable getTransform would bypass the dirty tracking).
    inline void setTransform(const Transform& transform)
    {
        Transform& current = Globals::renderNodeTransforms[m_transformIdx];
        if (current.pos == transform.pos && current.scale == transform.scale && current.quat == transform.quat)
            return;
        current = transform;
        markRenderNodeTransformDirty(m_transformIdx);
    }

    inline const Transform& getTransform() const
    {
        return Globals::renderNodeTransforms[m_transformIdx];
    }

    inline size_t getNumMeshInstances() const { return m_meshInstances.size(); }

    // Valid only for nodes spawned via ObjectContainer::spawnSkinnedNode. Pass to Renderer::setSkinningPalette
    // each frame with the AnimationPlayer's bone palette. UINT32_MAX for non-skinned nodes.
    inline bool isSkinned() const { return m_skinnedPaletteHandle != UINT32_MAX; }
    inline uint32 getSkinnedPaletteHandle() const { return m_skinnedPaletteHandle; }

    inline const Sphere& getLocalBounds() const { return m_bounds; }
    inline Sphere getWorldBounds() const
    {
        const Transform& transform = getTransform();
        return Sphere(transform.quat * m_bounds.pos * transform.scale + transform.pos, m_bounds.radius * transform.scale);
    };

private:

    friend class ObjectContainer;
    friend class Renderer;

    void moveFrom(RenderNode&& other) noexcept
    {
        m_transformIdx = other.m_transformIdx;
        m_skinnedPaletteHandle = other.m_skinnedPaletteHandle;
        m_skinnedBundleHandle = other.m_skinnedBundleHandle;
        m_bounds = other.m_bounds;
        m_meshInstances = std::move(other.m_meshInstances);
        m_numInstancesPerMesh = std::move(other.m_numInstancesPerMesh);
        m_lodInstances = std::move(other.m_lodInstances);
        other.m_transformIdx = UINT32_MAX;
        other.m_skinnedPaletteHandle = UINT32_MAX;
        other.m_skinnedBundleHandle = UINT32_MAX;
    }

    uint32 m_transformIdx = UINT32_MAX;
    uint32 m_skinnedPaletteHandle = UINT32_MAX;
    uint32 m_skinnedBundleHandle = UINT32_MAX;
    Sphere m_bounds;
    std::vector<RendererVKLayout::InMeshInstance> m_meshInstances;
    std::vector<std::pair<uint16, uint16>> m_numInstancesPerMesh;
    // Instances with a LOD chain: (index into m_meshInstances, Renderer mesh-LOD-group index). The
    // stored instance references the LOD0 mesh; Renderer::selectMeshLods redirects per frame.
    std::vector<std::pair<uint32, uint32>> m_lodInstances;
};
