module;

#include <OgreMatrix4.h>

#include <vector>
#include <memory>
#include <functional>
#include <entt/fwd.hpp>
#include <openvr.h>

export module Systems.InputSystem;

import Components.VRHandTrackingComponent;
import Utils.HandSkeletonData;

export class GraphicsSystem;
export class World;

export struct SDL_MouseWheelEvent;
export struct SDL_MouseMotionEvent;
export struct SDL_MouseButtonEvent;

export struct SDL_TextEditingEvent;
export struct SDL_TextInputEvent;
export struct SDL_KeyboardEvent;

export struct SDL_JoyButtonEvent;
export struct SDL_JoyAxisEvent;
export struct SDL_JoyHatEvent;

export enum SDL_Scancode;

export class MouseListener
{
public:
    std::function<void(const SDL_MouseMotionEvent&)> onMouseMoved;
    std::function<void(const SDL_MouseWheelEvent&)>  onMouseWheelMoved;
    std::function<void(const SDL_MouseButtonEvent&)> onMousePressed;
    std::function<void(const SDL_MouseButtonEvent&)> onMouseReleased;
};

export class KeyboardListener
{
public:
    std::function<void(const SDL_TextEditingEvent&)> onTextEditing;
    std::function<void(const SDL_TextInputEvent&)>   onTextInput;
    std::function<void(const SDL_KeyboardEvent&)>    onKeyPressed;
    std::function<void(const SDL_KeyboardEvent&)>    onKeyReleased;
};

export class JoystickListener
{
public:
    virtual void joyButtonPressed(const SDL_JoyButtonEvent& evt, int button) {}
    virtual void joyButtonReleased(const SDL_JoyButtonEvent& evt, int button) {}
    virtual void joyAxisMoved(const SDL_JoyAxisEvent& arg, int axis) {}
    virtual void joyPovMoved(const SDL_JoyHatEvent& arg, int index) {}
};

export class InputSystem
{
public:

    InputSystem(World& world, entt::registry& registry);
    virtual ~InputSystem();
    InputSystem(const InputSystem& copy) = delete;

    void initialize(GraphicsSystem* pGraphics);

    void update(double deltaSec);
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

    bool isKeyCurrentlyPressed(SDL_Scancode sc);

private:

    void updateVRInput();
    void updateMouseSettings();
    /// Internal method for ignoring relative
    /// motions as a side effect of warpMouse()
    bool handleWarpMotion(const SDL_MouseMotionEvent& evt);
    void getVRSkeletalData(HandSkeletonData& outData, vr::VRActionHandle_t skeletonActionHandle);

private:

    World& m_world;
    entt::registry& m_registry;

    const uint8_t* m_pKeyStates = nullptr;

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

