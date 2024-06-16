module;

#include <functional>
#include <memory>
#include <vector>

export module Input.Input;

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

export struct SDL_WindowEvent;

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

export class SystemEventListener
{
public:
    std::function<void(const SDL_WindowEvent&)> onWindowEvent;
    std::function<void()> onQuit;
};

export class JoystickListener
{
public:
    virtual void joyButtonPressed(const SDL_JoyButtonEvent& evt, int button) {}
    virtual void joyButtonReleased(const SDL_JoyButtonEvent& evt, int button) {}
    virtual void joyAxisMoved(const SDL_JoyAxisEvent& arg, int axis) {}
    virtual void joyPovMoved(const SDL_JoyHatEvent& arg, int index) {}
};

export class Input final
{
public:
	Input();
	~Input();
    Input(const Input&) = delete;

	bool initialize();
    
    void update_MT(double deltaSec);
    void update(double deltaSec);

private:

    const uint8_t* m_pKeyStates = nullptr;
    std::vector<std::unique_ptr<MouseListener>>    m_mouseListeners;
    std::vector<std::unique_ptr<KeyboardListener>> m_keyboardListeners;
    std::vector<std::unique_ptr<SystemEventListener>> m_systemEventListeners;

    bool m_mouseInWindow = false;
    bool m_windowHasFocus = false;
    bool m_wantMousieGrab = false;
    bool m_wantMouseVisible = true;
};