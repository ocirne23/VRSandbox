module Entity.FreeFlyCameraController;

import Core;
import Input;
import Core.SDL;
import UI;

FreeFlyCameraController::~FreeFlyCameraController()
{
    if (m_mouseListener)
        Globals::input.removeMouseListener(m_mouseListener);
}

void FreeFlyCameraController::initialize(glm::vec3 position, glm::vec3 lookAt, glm::vec3 up)
{
    m_position = position;
    m_direction = glm::normalize(lookAt - position);
    m_up = up;
    m_viewMatrix = glm::lookAt(position, lookAt, up);

    m_mouseListener = Globals::input.addMouseListener();
    m_mouseListener->onMousePressed = [this](const SDL_MouseButtonEvent& evt)
        {
            if (evt.button == 1)
            {
                m_isMouseDown = true;
            }
        };
    m_mouseListener->onMouseReleased = [this](const SDL_MouseButtonEvent& evt)
        {
            if (evt.button == 1)
            {
                m_isMouseDown = false;
                m_mousePosUpdated = false;
            }
        };
    m_mouseListener->onMouseMoved = [this](const SDL_MouseMotionEvent& evt)
        {
            if (!Globals::input.isWindowHasFocus() || !m_isMouseDown || !Globals::ui.isViewportFocused())
                return;
            if (!m_mousePosUpdated || Globals::ui.hasViewportGainedFocused())
            {
                m_lastMousePos = glm::vec2(evt.x, evt.y);
                m_mousePosUpdated = true;
            }
            glm::vec2 currentPos = glm::vec2(evt.x, evt.y);
            glm::vec2 delta = currentPos - m_lastMousePos;
            m_lastMousePos = currentPos;

            glm::vec3 right = glm::cross(m_direction, m_up);
            glm::mat4 rot = glm::rotate(glm::mat4(1.0f), -delta.x * m_sensitivity, m_up);
            rot = glm::rotate(rot, -delta.y * m_sensitivity, right);
            m_direction = glm::normalize(glm::vec3(rot * glm::vec4(m_direction, 0.0f)));
            m_up = glm::cross(right, m_direction);
        };
}

void FreeFlyCameraController::update(double deltaTime)
{
    const float deltaSec = static_cast<float>(deltaTime);
    Input& input = Globals::input;
    if (input.isWindowHasFocus() && Globals::ui.isViewportFocused())
    {
        const float boost = input.isKeyDown(SDL_SCANCODE_LSHIFT) ? m_boostMultiplier : 1.0f;

        if (input.isKeyDown(SDL_SCANCODE_W))
            m_position += m_direction * m_speed * deltaSec * boost;
        if (input.isKeyDown(SDL_SCANCODE_S))
            m_position -= m_direction * m_speed * deltaSec * boost;
        if (input.isKeyDown(SDL_SCANCODE_A))
            m_position -= glm::normalize(glm::cross(m_direction, m_up)) * m_speed * deltaSec * boost;
        if (input.isKeyDown(SDL_SCANCODE_D))
            m_position += glm::normalize(glm::cross(m_direction, m_up)) * m_speed * deltaSec * boost;
        if (input.isKeyDown(SDL_SCANCODE_SPACE))
            m_position += m_up * m_speed * deltaSec * boost;
        if (input.isKeyDown(SDL_SCANCODE_LCTRL))
            m_position -= m_up * m_speed * deltaSec * boost;
        if (input.isKeyDown(SDL_SCANCODE_Q))
            m_up = glm::normalize(glm::vec3(glm::rotate(glm::mat4(1.0f), -1.0f * deltaSec, m_direction) * glm::vec4(m_up, 0.0f)));
        if (input.isKeyDown(SDL_SCANCODE_E))
            m_up = glm::normalize(glm::vec3(glm::rotate(glm::mat4(1.0f), 1.0f * deltaSec, m_direction) * glm::vec4(m_up, 0.0f)));
    }

    m_viewMatrix = glm::lookAt(m_position, m_position + m_direction, m_up);
}