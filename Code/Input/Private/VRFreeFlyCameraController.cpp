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
    const float turn = deadzone(vrInput.getTurnAxis().x);

    // Right stick X = smooth yaw turn around world up.
    m_yaw = glm::normalize(glm::angleAxis(-turn * m_turnSpeed * dt, glm::vec3(0.0f, 1.0f, 0.0f)) * m_yaw);

    // Left stick = move relative to the play-space yaw (forward = -Z), so it follows where you've turned.
    const glm::vec3 forward = m_yaw * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = m_yaw * glm::vec3(1.0f, 0.0f, 0.0f);
    m_position += (right * deadzone(move.x) + forward * deadzone(move.y)) * m_moveSpeed * dt;
}
