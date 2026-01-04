export module RendererVK.RenderNode;
extern "C++" {

import Core;
import Core.glm;
import Core.Sphere;
import RendererVK;
import RendererVK.Layout;
import RendererVK.Transform;
import RendererVK.ObjectContainer;

export class RenderNode final
{
public:

    inline Transform& getTransform()
    {
        return Globals::rendererVK.getRenderNodeTransform(m_transformIdx);
    }

    inline const Transform& getTransform() const
    {
        return Globals::rendererVK.getRenderNodeTransform(m_transformIdx);
    }

    inline const Sphere& getLocalBounds() const { return m_bounds; }
    inline Sphere getWorldBounds() const 
    {
        const Transform& transform = getTransform();
        return Sphere(transform.quat * m_bounds.pos * transform.scale + transform.pos, m_bounds.radius * transform.scale);
    };

private:

    friend class ObjectContainer;
    friend class RendererVK;

    uint32 m_transformIdx = UINT32_MAX;
    Sphere m_bounds;
    std::vector<RendererVKLayout::InMeshInstance> m_meshInstances;
    std::vector<std::pair<uint16, uint16>> m_numInstancesPerMesh;
};
} // extern "C++"