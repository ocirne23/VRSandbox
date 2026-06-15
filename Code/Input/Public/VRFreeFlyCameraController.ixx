export module Input.VRFreeFlyCameraController;

import Core;
import Core.glm;
import Core.Camera;

// Thumbstick locomotion for VR: drives the play-space transform (position + yaw) from Globals::vrInput.
// The renderer composes the headset pose on top of this transform, so this controller only produces the
// play-space origin/orientation (no pitch/roll — those come from the head). Mirrors FreeFlyCameraController's
// getCamera() shape so main.cpp can swap between them by VR state.
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
        // The thumbstick yaw is the play-space turn; keep it solely in playSpaceOrientation. The view
        // matrix carries position only (identity rotation) so the renderer's camera-rotation base stays
        // identity and the yaw isn't applied twice. In VR the renderer overrides viewMatrix with the head pose.
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
