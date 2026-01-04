export module RendererVK.Camera;
extern "C++" {

import Core;
import Core.glm;

export struct Camera
{
public:

    glm::mat4 viewMatrix;
    glm::vec3 position;
    float fovDeg = 45.0f;
    float near = 0.1f;
    float far = 5000.0f;
};
} // extern "C++"