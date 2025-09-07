module Input;

import Core.SDL;
import Core.imgui;

import UI;

bool Input::initialize()
{
    m_pKeyStates = SDL_GetKeyboardState(nullptr);
    return true;
}

void Input::update_MT(double deltaSec)
{
    SDL_PumpEvents();
}

void Input::update(double deltaSec)
{
    ImGuiIO& imguiIO = ImGui::GetIO();

    SDL_Event evt;
    while (SDL_PollEvent(&evt))
    {
        ImGui_ImplSDL3_ProcessEvent(&evt);
        if (evt.type >= SDL_EVENT_KEY_DOWN && evt.type <= SDL_EVENT_TEXT_INPUT && (imguiIO.WantCaptureKeyboard || imguiIO.WantTextInput) && !Globals::ui.hasViewportFocused())
            continue;
        if (evt.type >= SDL_EVENT_MOUSE_MOTION && evt.type <= SDL_EVENT_MOUSE_WHEEL && (imguiIO.WantCaptureMouse && !Globals::ui.hasViewportFocused()))
            continue;

        switch (evt.type)
        {
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            m_windowHasFocus = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            m_windowHasFocus = false;
            break;
            /*
        case SDL_EVENT_WINDOWEVENT:
            for (auto& listener : m_systemEventListeners)
                if (listener->onWindowEvent) listener->onWindowEvent(evt.window);
            break;*/
        case SDL_EVENT_QUIT:
            for (auto& listener : m_systemEventListeners)
                if (listener->onQuit) listener->onQuit();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            for (auto& listener : m_mouseListeners)
                if (listener->onMouseMoved) listener->onMouseMoved(evt.motion);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            for (auto& listener : m_mouseListeners)
                if (listener->onMouseWheelMoved) listener->onMouseWheelMoved(evt.wheel);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            for (auto& listener : m_mouseListeners)
                if (listener->onMousePressed) listener->onMousePressed(evt.button);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            for (auto& listener : m_mouseListeners)
                if (listener->onMouseReleased) listener->onMouseReleased(evt.button);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (evt.key.repeat == 0)
            {
                for (auto& listener : m_keyboardListeners)
                    if (listener->onKeyPressed) listener->onKeyPressed(evt.key);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (evt.key.repeat == 0)
            {
                for (auto& listener : m_keyboardListeners)
                    if (listener->onKeyReleased) listener->onKeyReleased(evt.key);
            }
            break;
        default:
            break;
        }
    }
}