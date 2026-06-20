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

export class RenderNode final
{
public:

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

    uint32 m_transformIdx = UINT32_MAX;
    uint32 m_skinnedPaletteHandle = UINT32_MAX;
    Sphere m_bounds;
    std::vector<RendererVKLayout::InMeshInstance> m_meshInstances;
    std::vector<std::pair<uint16, uint16>> m_numInstancesPerMesh;
};