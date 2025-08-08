export module RendererVK.RenderNode;

import Core;
import Core.glm;
import RendererVK;
import RendererVK.Transform;
import RendererVK.ObjectContainer;

export class RenderNode final
{
public:

    RenderNode() = default;
    RenderNode(const RenderNode& copy) = delete;
    
    RenderNode(RenderNode&& move)
    {
        m_spawnIdx = move.m_spawnIdx;
        m_transformIdx = move.m_transformIdx;
        m_pObjectContainer = move.m_pObjectContainer;
    }

    RenderNode& operator=(RenderNode&& move)
    {
        m_spawnIdx = move.m_spawnIdx;
        m_transformIdx = move.m_transformIdx;
        m_pObjectContainer = move.m_pObjectContainer;
        return *this;
    }

    inline Transform& getTransform()
    {
        return Globals::rendererVK.getRenderNodeTransform(m_transformIdx);
    }

private:

    friend class ObjectContainer;

    RenderNode(ObjectContainer* pObjectContainer, NodeSpawnIdx spawnIdx, uint32 transformIdx)
        : m_pObjectContainer(pObjectContainer), m_spawnIdx(spawnIdx), m_transformIdx(transformIdx)
    {
    }

    uint32 m_transformIdx = 0;
    ObjectContainer* m_pObjectContainer = nullptr;
    NodeSpawnIdx m_spawnIdx = NodeSpawnIdx_INVALID;
};