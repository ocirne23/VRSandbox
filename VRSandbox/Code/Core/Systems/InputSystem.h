#pragma once

#include "Utils/OpenVRInclude.h"

#include <OgreMatrix4.h>

#include <vector>
#include <memory>
#include <functional>
#include <entt/fwd.hpp>


namespace vr {
    typedef uint64_t VRActionHandle_t;
}
class GraphicsSystem;

struct SDL_MouseWheelEvent;
struct SDL_MouseMotionEvent;
struct SDL_MouseButtonEvent;

struct SDL_TextEditingEvent;
struct SDL_TextInputEvent;
struct SDL_KeyboardEvent;

struct SDL_JoyButtonEvent;
struct SDL_JoyAxisEvent;
struct SDL_JoyHatEvent;

enum HandSkeletonBone : int32_t
{
    eBone_Root = 0,
    eBone_Wrist,
    eBone_Thumb0,
    eBone_Thumb1,
    eBone_Thumb2,
    eBone_Thumb3,
    eBone_IndexFinger0,
    eBone_IndexFinger1,
    eBone_IndexFinger2,
    eBone_IndexFinger3,
    eBone_IndexFinger4,
    eBone_MiddleFinger0,
    eBone_MiddleFinger1,
    eBone_MiddleFinger2,
    eBone_MiddleFinger3,
    eBone_MiddleFinger4,
    eBone_RingFinger0,
    eBone_RingFinger1,
    eBone_RingFinger2,
    eBone_RingFinger3,
    eBone_RingFinger4,
    eBone_PinkyFinger0,
    eBone_PinkyFinger1,
    eBone_PinkyFinger2,
    eBone_PinkyFinger3,
    eBone_PinkyFinger4,
    eBone_Aux_Thumb,
    eBone_Aux_IndexFinger,
    eBone_Aux_MiddleFinger,
    eBone_Aux_RingFinger,
    eBone_Aux_PinkyFinger,
    eBone_Count
};

struct HandSkeletonData
{
    Ogre::Matrix4 handTransform;
    struct
    {
        Ogre::Vector4 bonePos;
        Ogre::Quaternion boneRot;
    } boneTransforms[HandSkeletonBone::eBone_Count];
};

class MouseListener
{
public:
    std::function<void(const SDL_MouseMotionEvent&)> onMouseMoved;
    std::function<void(const SDL_MouseWheelEvent&)>  onMouseWheelMoved;
    std::function<void(const SDL_MouseButtonEvent&)> onMousePressed;
    std::function<void(const SDL_MouseButtonEvent&)> onMouseReleased;
};

class KeyboardListener
{
public:
    std::function<void(const SDL_TextEditingEvent&)> onTextEditing;
    std::function<void(const SDL_TextInputEvent&)>   onTextInput;
    std::function<void(const SDL_KeyboardEvent&)>    onKeyPressed;
    std::function<void(const SDL_KeyboardEvent&)>    onKeyReleased;
};

class JoystickListener
{
public:
    virtual void joyButtonPressed(const SDL_JoyButtonEvent& evt, int button) {}
    virtual void joyButtonReleased(const SDL_JoyButtonEvent& evt, int button) {}
    virtual void joyAxisMoved(const SDL_JoyAxisEvent& arg, int axis) {}
    virtual void joyPovMoved(const SDL_JoyHatEvent& arg, int index) {}
};

class InputSystem
{
public:

	InputSystem(GraphicsSystem* pGraphics);
    virtual ~InputSystem();
    InputSystem(const InputSystem &copy) = delete;

    void update(double deltaSec, entt::registry& registry);
    bool hasQuit() const { return m_hasQuit; }
    void setWantMouseGrab(bool grab) { m_wantMouseGrab = grab; updateMouseSettings(); }
    void setWantMouseRelative(bool relative) { m_wantMouseRelative = relative; updateMouseSettings(); }
    void setWantMouseVisible(bool visible) { m_wantMouseVisible = visible; updateMouseSettings(); }
    void warpMouse(int x, int y);
    /// Prevents the mouse cursor from leaving the window.
    void wrapMousePointer(const SDL_MouseMotionEvent& evt);

    MouseListener* createMouseListener();
    KeyboardListener* createKeyboardListener();
    void destroyMouseListener(MouseListener* pMouseListener);
    void destroyKeyboardListener(KeyboardListener* pKeyboardListener);

    const bool hasHandSkeletonData() const { return m_hasHandSkeletonData; }
    const HandSkeletonData& getLeftHandSkeletonData() const { return m_leftHandSkeletonData; }
    const HandSkeletonData& getRightHandSkeletonData() const { return m_rightHandSkeletonData; }

private:

    void updateVRInput();
    void updateMouseSettings();
    /// Internal method for ignoring relative
    /// motions as a side effect of warpMouse()
    bool handleWarpMotion(const SDL_MouseMotionEvent& evt);
    void getVRSkeletalData(HandSkeletonData& outData, vr::VRActionHandle_t skeletonActionHandle);

private:

    GraphicsSystem* m_pGraphics = nullptr;
    bool m_hasQuit = false;
    bool m_mouseInWindow = false;
    bool m_windowHasFocus = true;
    bool m_wantMouseGrab = false;
    bool m_wantMouseRelative = false;
    bool m_wantMouseVisible = true;
    bool m_grabPointer = false;
    bool m_isMouseRelative = false;
    bool m_wrapPointerManually = false;
    int  m_warpX = 0;
    int  m_warpY = 0;
    bool m_warpCompensate = false;
    bool m_hasHandSkeletonData = false;

    vr::VRActionSetHandle_t m_actionsetDemo = vr::k_ulInvalidActionSetHandle;

    vr::VRActionHandle_t m_actionLeftPose = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t m_actionRightPose = vr::k_ulInvalidActionHandle;

    vr::VRActionHandle_t m_actionLeftHaptic = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t m_actionRightHaptic = vr::k_ulInvalidActionHandle;

    vr::VRActionHandle_t m_actionLeftHandSkeleton = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t m_actionRightHandSkeleton = vr::k_ulInvalidActionHandle;

    HandSkeletonData m_leftHandSkeletonData;
    HandSkeletonData m_rightHandSkeletonData;

private:

    std::vector<std::unique_ptr<MouseListener>>    m_mouseListeners;
    std::vector<std::unique_ptr<KeyboardListener>> m_keyboardListeners;
    //std::vector<std::unique_ptr<JoystickListener>> m_joystickListeners;
};

