export module Input;

import Core;

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

export enum SDL_Scancode : int;

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

    Input() = default;
    ~Input() = default;
    Input(const Input&) = delete;
    Input(const Input&&) = delete;
    Input& operator=(const Input&) = delete;
    Input& operator=(const Input&&) = delete;

    bool initialize();

    void update_MT(double deltaSec);
    void update(double deltaSec);

    void setMouseInWindow(bool inWindow)       { m_mouseInWindow = inWindow; }
    void setWindowHasFocus(bool hasFocus)      { m_windowHasFocus = hasFocus; }
    void setWantMouseGrab(bool wantGrab)       { m_wantMousieGrab = wantGrab; }
    void setWantMouseVisible(bool wantVisible) { m_wantMouseVisible = wantVisible; }

    bool isKeyDown(SDL_Scancode key) const { return m_pKeyStates[key]; }
    bool isMouseInWindow() const           { return m_mouseInWindow; }
    bool isWindowHasFocus() const          { return m_windowHasFocus; }
    bool wantMouseGrab() const             { return m_wantMousieGrab; }
    bool wantMouseVisible() const          { return m_wantMouseVisible; }

    MouseListener* addMouseListener()             { return m_mouseListeners.emplace_back(std::make_unique<MouseListener>()).get(); }
    KeyboardListener* addKeyboardListener()       { return m_keyboardListeners.emplace_back(std::make_unique<KeyboardListener>()).get(); }
    SystemEventListener* addSystemEventListener() { return m_systemEventListeners.emplace_back(std::make_unique<SystemEventListener>()).get(); }

    void removeMouseListener(const MouseListener* listener)
    {
        for (auto it = m_mouseListeners.begin(); it != m_mouseListeners.end(); ++it)
        {
            if (it->get() == listener)
            {
                m_mouseListeners.erase(it);
                return;
            }
        }
    }
    void removeKeyboardListener(const KeyboardListener* listener)
    {
        for (auto it = m_keyboardListeners.begin(); it != m_keyboardListeners.end(); ++it)
        {
            if (it->get() == listener)
            {
                m_keyboardListeners.erase(it);
                return;
            }
        }
    }
    void removeSystemEventListener(const SystemEventListener* listener)
    {
        for (auto it = m_systemEventListeners.begin(); it != m_systemEventListeners.end(); ++it)
        {
            if (it->get() == listener)
            {
                m_systemEventListeners.erase(it);
                return;
            }
        }
    }

private:

    const bool* m_pKeyStates = nullptr;
    std::vector<std::unique_ptr<MouseListener>>    m_mouseListeners;
    std::vector<std::unique_ptr<KeyboardListener>> m_keyboardListeners;
    std::vector<std::unique_ptr<SystemEventListener>> m_systemEventListeners;

    bool m_mouseInWindow = false;
    bool m_windowHasFocus = false;
    bool m_wantMousieGrab = false;
    bool m_wantMouseVisible = true;
};

export namespace Globals
{
    Input input;
}