export module Input:VRFreeFlyCameraController;

import Core;
import Core.glm;
import Core.Camera;

export class VRFreeFlyCameraController final
{
public:

    VRFreeFlyCameraController() {}
    ~VRFreeFlyCameraController() {}
    VRFreeFlyCameraController(const VRFreeFlyCameraController&) = delete;

    void initialize(glm::vec3 position);
    void update(double deltaTime);

    void setSpeed(float metresPerSecond) { m_moveSpeed = metresPerSecond; }
    void setTurnSpeed(float radiansPerSecond) { m_turnSpeed = radiansPerSecond; }

    glm::vec3 getPosition() const { return m_position; }
    glm::quat getOrientation() const { return m_yaw; }

    Camera getCamera() const
    {
        Camera camera;
        camera.position = m_position;
        camera.playSpaceOrientation = m_yaw;
        camera.viewMatrix = glm::translate(glm::mat4(1.0f), -m_position);
        return camera;
    }

private:

    glm::vec3 m_position = glm::vec3(0.0f);
    glm::quat m_yaw = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    float m_moveSpeed = 3.0f;                  // metres / second
    float m_turnSpeed = glm::radians(90.0f);   // radians / second (smooth turn)
    float m_deadzone = 0.15f;
    float m_verticalDeadzone = 0.6f;           // big: right-stick Y is vertical move, must not trip while turning (X)
};
