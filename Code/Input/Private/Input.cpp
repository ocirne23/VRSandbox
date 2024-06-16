module;

#include <SDL3/SDL.h>

module Input.Input;

Input::Input() {}
Input::~Input() {}

bool Input::initialize()
{
	return true;
}

void Input::update_MT(double deltaSec)
{
	SDL_PumpEvents();
}

void Input::update(double deltaSec)
{
	SDL_Event evt;
	while (SDL_PollEvent(&evt))
	{

		switch (evt.type)
		{
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
			for (auto& listener : m_keyboardListeners)
				if (listener->onKeyPressed) listener->onKeyPressed(evt.key);
			break;
		case SDL_EVENT_KEY_UP:
			for (auto& listener : m_keyboardListeners)
				if (listener->onKeyReleased) listener->onKeyReleased(evt.key);
			break;
		default:
			break;
		}
	}
}