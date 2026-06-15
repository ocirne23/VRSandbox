export module Core.Camera;

import Core.glm;

export struct Camera
{
public:

    glm::mat4 viewMatrix;
    glm::vec3 position;
    // VR only: the play-space yaw. Locomotion turn rotates this; the renderer composes the headset pose
    // on top of it (T(position) * R(playSpaceOrientation) * headPose). Identity on desktop (unused).
    glm::quat playSpaceOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    float fovDeg = 45.0f;
    float near = 0.1f;
    float far = 5000.0f;
};