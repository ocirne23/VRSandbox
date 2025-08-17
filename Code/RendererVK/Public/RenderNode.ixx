export module RendererVK.RenderNode;

import Core;
import Core.glm;
import RendererVK;
import RendererVK.Layout;
import RendererVK.Transform;
import RendererVK.ObjectContainer;

export class RenderNode final
{
public:

    RenderNode() = default;
    RenderNode(const RenderNode& copy) = delete;
    
    RenderNode(RenderNode&& move)
    {
        m_transformIdx = move.m_transformIdx;
        m_meshInstances = std::move(move.m_meshInstances);
        m_numInstancesPerMesh = std::move(move.m_numInstancesPerMesh);
    }

    RenderNode& operator=(RenderNode&& move)
    {
        m_transformIdx = move.m_transformIdx;
        m_meshInstances = std::move(move.m_meshInstances);
        m_numInstancesPerMesh = std::move(move.m_numInstancesPerMesh);
        return *this;
    }

    inline Transform& getTransform()
    {
        return Globals::rendererVK.getRenderNodeTransform(m_transformIdx);
    }

private:

    friend class ObjectContainer;
    friend class RendererVK;

    uint32 m_transformIdx = UINT32_MAX;
    std::vector<RendererVKLayout::InMeshInstance> m_meshInstances;
    std::vector<std::pair<uint16, uint16>> m_numInstancesPerMesh;
};