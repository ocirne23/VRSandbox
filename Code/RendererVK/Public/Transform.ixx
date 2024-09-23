export module RendererVK.Transform;

import Core.glm;

export struct Transform
{
    Transform() : pos(0, 0, 0), scale(1.0f), quat(1, 0, 0, 0) {}
    glm::vec3 pos;
    float scale;
    glm::quat quat;
};