export module Core.Camera;

import Core.glm;
import Core.Rect;

export struct Camera
{
public:

    // Unprojects a viewport-space screen point into the world: a ray through the camera is intersected
    // with the ground plane (y = 0); if it doesn't hit, the point lands a fixed distance ahead.
    glm::vec3 screenToWorld(const Rect& viewport, glm::vec2 screenPos) const
    {
        const glm::vec2 size = glm::vec2(viewport.getSize());
        if (size.x <= 0.0f || size.y <= 0.0f)
            return position;

        const float ndcX = (screenPos.x - viewport.min.x) / size.x * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screenPos.y - viewport.min.y) / size.y * 2.0f;
        const float tanHalfFov = glm::tan(glm::radians(fovDeg) * 0.5f);
        const float aspect = size.x / size.y;

        const glm::vec3 viewDir = glm::normalize(glm::vec3(ndcX * tanHalfFov * aspect, ndcY * tanHalfFov, -1.0f));
        const glm::mat4 invView = glm::inverse(viewMatrix);
        const glm::vec3 worldDir = glm::normalize(glm::vec3(invView * glm::vec4(viewDir, 0.0f)));

        if (glm::abs(worldDir.y) > 1e-4f)
        {
            const float t = -position.y / worldDir.y;
            if (t > 0.0f && t < 1000.0f)
                return position + worldDir * t;
        }
        return position + worldDir * 15.0f;
    }

    glm::mat4 viewMatrix;
    glm::vec3 position;
    // VR only: the play-space yaw. Locomotion turn rotates this; the renderer composes the headset pose
    // on top of it (T(position) * R(playSpaceOrientation) * headPose). Identity on desktop (unused).
    glm::quat playSpaceOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    float fovDeg = 45.0f;
    float near = 0.01f;
    float far = 5000.0f;
};