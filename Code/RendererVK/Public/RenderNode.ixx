export module RendererVK.RenderNode;

import Core;
import Core.glm;
import RendererVK.Transform;
import RendererVK.ObjectContainer;

export class RenderNode final
{
public:

    RenderNode() = default;
    RenderNode(const RenderNode& copy) = delete;
    
    RenderNode(RenderNode&& move)
    {
        m_flags    = move.m_flags;
        m_numNodes = move.m_numNodes;
        m_nodeIdx  = move.m_nodeIdx;
        m_pObjectContainer = move.m_pObjectContainer;
    }

    RenderNode& operator=(RenderNode&& move)
    {
        m_flags = move.m_flags;
        m_numNodes = move.m_numNodes;
        m_nodeIdx = move.m_nodeIdx;
        m_pObjectContainer = move.m_pObjectContainer;
        return *this;
    }

    inline Transform& getTransform()
    {
        return m_pObjectContainer->m_renderNodes[m_nodeIdx].transform;
    }

private:

    friend class ObjectContainer;

    RenderNode(ObjectContainer* pObjectContainer, uint32 nodeIdx, uint16 numNodes)
        : m_pObjectContainer(pObjectContainer), m_nodeIdx(nodeIdx), m_numNodes(numNodes)
    {
    }

    uint16 m_flags    = 0;
    uint16 m_numNodes = 0;
    uint32 m_nodeIdx  = 0;
    ObjectContainer* m_pObjectContainer = nullptr;
};