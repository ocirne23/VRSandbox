export module Input:FreeFlyCameraController;

import Core;
import Core.glm;
import Core.Camera;

import Input.fwd;

export class FreeFlyCameraController final
{
public:

    FreeFlyCameraController() {}
    ~FreeFlyCameraController();
    FreeFlyCameraController(const FreeFlyCameraController&) = delete;

    void initialize(glm::vec3 position, glm::vec3 lookAt, glm::vec3 up = glm::vec3(0, 1, 0));
    void update(double deltaTime);
    void setSpeed(float speed) { m_speed = speed; }
    void setSensitivity(float sensitivity) { m_sensitivity = sensitivity; }

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getDirection() const { return m_direction; }
    glm::vec3 getUp() const { return m_up; }
    glm::mat4 getViewMatrix() const { return m_viewMatrix; }
	glm::quat getOrientation() const { return glm::quat_cast(glm::inverse(m_viewMatrix)); }

    Camera getCamera()
    {
        Camera camera;
        camera.position = m_position;
        camera.viewMatrix = m_viewMatrix;
        camera.near = m_near;
        camera.far = m_far;
        return camera;
    }

private:

    MouseListener* m_mouseListener = nullptr;

    bool m_isMouseDown = false;
    bool m_mousePosUpdated = false;
    float m_boostMultiplier = 10.0f;
    float m_speed = 5.0f;
    float m_sensitivity = 0.01f;
    float m_near = 0.01f;
    float m_far = 5000.0f;
    glm::vec2 m_lastMousePos;
    glm::vec3 m_position;
    glm::vec3 m_direction;
    glm::vec3 m_up;
    glm::mat4 m_viewMatrix;
};
