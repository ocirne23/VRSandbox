module;

#include <openxr/openxr.h>

export module Input.VrInput;

import Core;
import Core.glm;
import Core.VrSession;

export enum class EVrHand : uint32 { Left = 0, Right = 1, Count = 2 };

// Per-controller state latched each frame. Poses are in the OpenXR tracking (LOCAL) space, i.e. relative
// to the play-space origin; the caller composes the same play-space transform the renderer applies to the
// head (T(playPos) * R(playYaw) * pose) to get world space. Buttons map A/B (right) and X/Y (left) on
// Touch-style controllers; trigger/grip are analog 0..1.
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

// VR controller input via OpenXR actions: thumbstick locomotion (left = move, right = turn), per-hand grip
// poses, trigger/grip analog, and face buttons. The OpenXR session is owned by the renderer and reached
// through the shared Core IVrSession interface (bridged in main.cpp), so the Input lib stays decoupled from
// RendererVK (it links only the OpenXR loader). Exposed as the Globals::vrInput singleton.
export class VrInput final
{
public:
    VrInput() {}
    ~VrInput() { destroy(); }
    VrInput(const VrInput&) = delete;
    VrInput& operator=(const VrInput&) = delete;

    // Create the action set, actions, controller pose spaces, suggest bindings, and attach to the session.
    // No-op returning false when vrSession is null (VR disabled). Call once after the renderer's session
    // exists; the interface is retained to read the live predicted display time + reference space.
    bool initialize(IVrSession* vrSession);

    // Sync the action set and latch this frame's thumbsticks, buttons, and controller poses (located at the
    // session's current predicted display time). Safe every frame; everything reads zero/inactive until the
    // session is focused.
    void update();

    void destroy();

    bool isActive() const { return m_active; }            // a thumbstick delivered input this frame
    glm::vec2 getMoveAxis() const { return m_moveAxis; }  // left stick: x = strafe, y = forward
    glm::vec2 getTurnAxis() const { return m_turnAxis; }  // right stick: x = yaw
    const VrHandState& getHand(EVrHand hand) const { return m_hands[(uint32)hand]; }

private:

    static constexpr uint32 HAND_COUNT = (uint32)EVrHand::Count;

    IVrSession* m_vrSession = nullptr; // not owned (renderer-owned); provides live display time + handles
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_referenceSpace = XR_NULL_HANDLE; // owned by the renderer; not destroyed here
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
    bool m_enabled = false;
    bool m_active = false;
};

export namespace Globals
{
    VrInput vrInput;
}
