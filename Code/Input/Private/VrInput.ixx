module;

#include <openxr/openxr.h>

export module Input:VrInput;

import Core;
import Core.glm;
import Core.VrSession;

export enum class EVrHand : uint32 { Left = 0, Right = 1, Count = 2 };

export struct VrHandState
{
    bool poseValid = false;
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    float trigger = 0.0f;          // index trigger 0..1
    float grip = 0.0f;             // squeeze / grip 0..1
    bool primaryButton = false;    // A (right) / X (left)
    bool secondaryButton = false;  // B (right) / Y (left)
};

export class VrInput final
{
public:
    VrInput() {}
    ~VrInput() { destroy(); }
    VrInput(const VrInput&) = delete;
    VrInput& operator=(const VrInput&) = delete;

    bool initialize(IVrSession* vrSession);
    void update();
    void destroy();

    bool isActive() const { return m_active; }            // a thumbstick delivered input this frame
    glm::vec2 getMoveAxis() const { return m_moveAxis; }  // left stick: x = strafe, y = forward
    glm::vec2 getTurnAxis() const { return m_turnAxis; }  // right stick: x = yaw
    const VrHandState& getHand(EVrHand hand) const { return m_hands[(uint32)hand]; }

    // Headset pose in the tracking (LOCAL / play) space, i.e. relative to the play-space origin. Compose the
    // play-space transform (as the renderer does) for world space. Used for head-relative locomotion.
    bool isHeadPoseValid() const { return m_headPoseValid; }
    glm::quat getHeadOrientation() const { return m_headOrientation; }
    glm::vec3 getHeadPosition() const { return m_headPosition; }

private:

    static constexpr uint32 HAND_COUNT = (uint32)EVrHand::Count;

    IVrSession* m_vrSession = nullptr; // not owned (renderer-owned); provides live display time + handles
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_referenceSpace = XR_NULL_HANDLE; // owned by the renderer; not destroyed here
    XrSpace m_viewSpace = XR_NULL_HANDLE;      // VIEW space (head pose); owned + destroyed here
    XrActionSet m_actionSet = XR_NULL_HANDLE;
    XrAction m_moveAction = XR_NULL_HANDLE;
    XrAction m_turnAction = XR_NULL_HANDLE;
    XrAction m_poseAction[HAND_COUNT] = {};
    XrAction m_triggerAction[HAND_COUNT] = {};
    XrAction m_gripAction[HAND_COUNT] = {};
    XrAction m_primaryAction[HAND_COUNT] = {};
    XrAction m_secondaryAction[HAND_COUNT] = {};
    XrSpace m_poseSpace[HAND_COUNT] = {};

    glm::vec2 m_moveAxis{ 0.0f };
    glm::vec2 m_turnAxis{ 0.0f };
    VrHandState m_hands[HAND_COUNT];
    bool m_headPoseValid = false;
    glm::quat m_headOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 m_headPosition = glm::vec3(0.0f);
    bool m_enabled = false;
    bool m_active = false;
};

export namespace Globals
{
    VrInput vrInput;
}
