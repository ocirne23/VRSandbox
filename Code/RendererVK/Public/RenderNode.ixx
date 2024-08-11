export module RendererVK.RenderNode;

import Core;
import Core.glm;

export class RenderNode final
{
public:

    glm::vec3 m_pos;
    float m_scale;
    glm::quat m_quat;

private:

    friend class ObjectContainer;

    uint16 m_flags           = 0;
    uint16 m_meshInfoIdx     = USHRT_MAX;
    uint16 m_meshInstanceIdx = USHRT_MAX;
    uint16 m_numChildren     = 0;
    uint32 m_parentIdx       = 0;
};