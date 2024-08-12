export module RendererVK.RenderNode;

import Core;
import Core.glm;

export class ObjectContainer;

export class RenderNode final
{
public:

    RenderNode(const RenderNode& copy) = default;
    RenderNode(RenderNode&& move)
    {
        m_pObjectContainer = move.m_pObjectContainer;
        m_nodeIdx = move.m_nodeIdx;
    }

private:

    friend class ObjectContainer;

    RenderNode(ObjectContainer* pObjectContainer, uint32 nodeIdx, uint16 numNodes)
        : m_pObjectContainer(pObjectContainer), m_nodeIdx(nodeIdx), m_numNodes(numNodes)
    {
    }

    uint16 m_flags = 0;
    uint16 m_numNodes = 0;
    uint32 m_nodeIdx = 0;
    ObjectContainer* m_pObjectContainer = nullptr;
};