export module RendererVK.Transform;
extern "C++" {

import Core.glm;

export struct Transform
{
    Transform() : pos(0, 0, 0), scale(1.0f), quat(1, 0, 0, 0) {}
    Transform(const glm::vec3& pos, float scale, const glm::quat& quat) : pos(pos), scale(scale), quat(quat) {}
    glm::vec3 pos;
    float scale;
    glm::quat quat;
};

} // extern "C++"