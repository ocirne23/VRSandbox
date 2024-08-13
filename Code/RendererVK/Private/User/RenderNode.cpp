module RendererVK.RenderNode;

import Core;
import RendererVK.ObjectContainer;

RenderNode RenderNode::cloneNode(glm::vec3 pos, float scale, glm::quat quat)
{
    return m_pObjectContainer->cloneNode(*this, pos, scale, quat);
}

void RenderNode::updateRenderTransform()
{
    m_pObjectContainer->updateRenderTransform(*this);
}