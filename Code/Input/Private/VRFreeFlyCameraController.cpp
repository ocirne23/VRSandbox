module Input.VRFreeFlyCameraController;

import Core;
import Core.glm;
import Input.VrInput;

void VRFreeFlyCameraController::initialize(glm::vec3 position)
{
    m_position = position;
    m_yaw = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void VRFreeFlyCameraController::update(double deltaTime)
{
    VrInput& vrInput = Globals::vrInput;
    vrInput.update();

    const float dt = static_cast<float>(deltaTime);
    const float dz2 = m_deadzone * m_deadzone;
    auto deadzone = [dz2](float v) { return (v * v < dz2) ? 0.0f : v; };

    const glm::vec2 move = vrInput.getMoveAxis();
    const glm::vec2 rightStick = vrInput.getTurnAxis();
    const float turn = deadzone(rightStick.x);

    // Right stick X = smooth yaw turn around world up.
    m_yaw = glm::normalize(glm::angleAxis(-turn * m_turnSpeed * dt, glm::vec3(0.0f, 1.0f, 0.0f)) * m_yaw);

    // Left stick = move along the headset's world look direction, so "up" always flies toward where you're
    // looking. World head orientation = play-space yaw * the head's tracking-space orientation (the renderer's
    // camera-rotation base is identity for this controller). Falls back to the play yaw before head tracking.
    const glm::quat headWorld = vrInput.isHeadPoseValid() ? (m_yaw * vrInput.getHeadOrientation()) : m_yaw;
    const glm::vec3 forward = headWorld * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = headWorld * glm::vec3(1.0f, 0.0f, 0.0f);
    m_position += (right * deadzone(move.x) + forward * deadzone(move.y)) * m_moveSpeed * dt;

    // Right stick Y = vertical move (world up/down). Large deadzone so it doesn't trip while turning (X).
    const float vdz2 = m_verticalDeadzone * m_verticalDeadzone;
    const float vertical = (rightStick.y * rightStick.y < vdz2) ? 0.0f : rightStick.y;
    m_position.y += vertical * m_moveSpeed * dt;
}
