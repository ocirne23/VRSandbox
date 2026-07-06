export module RendererVK:RenderNode;

import Core;
import Core.glm;
import Core.Sphere;
import Core.Transform;

import :Layout;

export namespace Globals
{
    std::vector<Transform> renderNodeTransforms;
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

    inline Transform& getTransform()
    {
        return Globals::renderNodeTransforms[m_transformIdx];
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
};
