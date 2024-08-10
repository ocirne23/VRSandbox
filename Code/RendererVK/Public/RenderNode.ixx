export module RendererVK.RenderNode;

import Core;
import Core.glm;

export class RenderNode final
{
public:

private:

    glm::vec3 m_pos;
    float scale;
    glm::quat m_quat;

    uint32 m_meshIdx = ~(0u);
};