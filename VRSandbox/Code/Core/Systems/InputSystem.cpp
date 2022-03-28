#include "InputSystem.h"

#include "GraphicsSystem.h"

#include <OgreWindowEventUtilities.h>
#include <SDL.h>
#include <OgreLogManager.h>

InputSystem::InputSystem(GraphicsSystem* pGraphics) : m_pGraphics(pGraphics)
{
}

InputSystem::~InputSystem()
{

}

void InputSystem::update(double deltaSec, entt::registry& registry)
{
	Ogre::WindowEventUtilities::messagePump();

	SDL_Event evt;
	while (SDL_PollEvent(&evt))
	{
		switch (evt.type)
		{
		case SDL_WINDOWEVENT:
			m_pGraphics->handleWindowEvent(evt);
			switch (evt.window.event)
			{
			case SDL_WINDOWEVENT_ENTER:
				m_mouseInWindow = true;
				updateMouseSettings();
				break;
			case SDL_WINDOWEVENT_LEAVE:
				m_mouseInWindow = false;
				updateMouseSettings();
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				m_windowHasFocus = true;
				updateMouseSettings();
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				m_windowHasFocus = false;
				updateMouseSettings();
				break;
			}
			break;
		case SDL_QUIT:
			m_hasQuit = true;
			break;
		case SDL_MOUSEMOTION:
			if (!handleWarpMotion(evt.motion))
			{
				// If in relative mode, don't trigger events unless window has focus
				if ((!m_wantMouseRelative || m_windowHasFocus))
					for (auto& listener : m_mouseListeners) 
						if (listener->onMouseMoved) listener->onMouseMoved(evt.motion);
				// Try to keep the mouse inside the window
				if (m_windowHasFocus)
					wrapMousePointer(evt.motion);
			}
			break;
		case SDL_MOUSEWHEEL:
			for (auto& listener : m_mouseListeners)
				if (listener->onMouseWheelMoved) listener->onMouseWheelMoved(evt.wheel);
			break;
		case SDL_MOUSEBUTTONDOWN:
			for (auto& listener : m_mouseListeners)
				if (listener->onMousePressed) listener->onMousePressed(evt.button);
			break;
		case SDL_MOUSEBUTTONUP:
			for (auto& listener : m_mouseListeners)
				if (listener->onMouseReleased) listener->onMouseReleased(evt.button);
			break;
		case SDL_KEYDOWN:
			for (auto& listener : m_keyboardListeners)
				if (listener->onKeyPressed) listener->onKeyPressed(evt.key);
			break;
		case SDL_KEYUP:
			for (auto& listener : m_keyboardListeners)
				if (listener->onKeyReleased) listener->onKeyReleased(evt.key);
			break;
		default:
			break;
		}
	}
}

void InputSystem::updateMouseSettings()
{
	m_grabPointer = m_wantMouseGrab && m_mouseInWindow && m_windowHasFocus;
	SDL_SetWindowGrab(m_pGraphics->getSDLWindow(), m_grabPointer ? SDL_TRUE : SDL_FALSE);
	SDL_ShowCursor(m_wantMouseVisible || !m_windowHasFocus);

	bool relative = m_wantMouseRelative && m_mouseInWindow && m_windowHasFocus;
	if (m_isMouseRelative == relative)
		return;

	m_isMouseRelative = relative;
	m_wrapPointerManually = false;

	//Input driver doesn't support relative positioning. Do it manually.
	int success = SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE);
	if (!relative || (relative && success != 0))
		m_wrapPointerManually = true;

	//Remove all pending mouse events that were queued with the old settings.
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_MOUSEMOTION);
}

void InputSystem::wrapMousePointer(const SDL_MouseMotionEvent& evt)
{
	//Don't wrap if we don't want relative movements, support
	//relative movements natively, or aren't grabbing anyways
	if (m_isMouseRelative || !m_wrapPointerManually || !m_grabPointer)
		return;

	int width = 0;
	int height = 0;

	SDL_GetWindowSize(m_pGraphics->getSDLWindow(), &width, &height);

	const int centerScreenX = width >> 1;
	const int centerScreenY = height >> 1;

	const int FUDGE_FACTOR_X = (width >> 2) - 1;
	const int FUDGE_FACTOR_Y = (height >> 2) - 1;

	//Warp the mouse if it's about to go outside the window
	if (evt.x <= centerScreenX - FUDGE_FACTOR_X || evt.x >= centerScreenX + FUDGE_FACTOR_X ||
		evt.y <= centerScreenY - FUDGE_FACTOR_Y || evt.y >= centerScreenY + FUDGE_FACTOR_Y)
	{
		warpMouse(centerScreenX, centerScreenY);
	}
}

MouseListener* InputSystem::createMouseListener()
{
	return m_mouseListeners.emplace_back(new MouseListener()).get();
}

KeyboardListener* InputSystem::createKeyboardListener()
{
	return m_keyboardListeners.emplace_back(new KeyboardListener()).get();
}

void InputSystem::destroyMouseListener(MouseListener* pMouseListener)
{
	auto it = std::find_if(m_mouseListeners.begin(), m_mouseListeners.end(), [pMouseListener](std::unique_ptr<MouseListener>& p)
		{
			return p.get() == pMouseListener;
		});
	assert(it != m_mouseListeners.end());
	m_mouseListeners.erase(it);
}

void InputSystem::destroyKeyboardListener(KeyboardListener* pKeyboardListener)
{
	auto it = std::find_if(m_keyboardListeners.begin(), m_keyboardListeners.end(), [pKeyboardListener](std::unique_ptr<KeyboardListener>& p)
		{
			return p.get() == pKeyboardListener;
		});
	assert(it != m_keyboardListeners.end());
	m_keyboardListeners.erase(it);
}

bool InputSystem::handleWarpMotion(const SDL_MouseMotionEvent& evt)
{
	if (!m_warpCompensate)
		return false;

	//This was a warp event, signal the caller to eat it.
	//The if statement is broken in Windows 10, an issue with SetCursorPos. See:
	//https://github.com/gottebp/windows10-setcursorpos-bug-demonstration
	//https://discourse.libsdl.org/t/win10-fall-creators-update-breaks-mouse-warping/23526/2
	//if( evt.x == mWarpX && evt.y == mWarpY )
	{
		m_warpCompensate = false;
		return true;
	}

	return false;
}

void InputSystem::warpMouse(int x, int y)
{
	SDL_WarpMouseInWindow(m_pGraphics->getSDLWindow(), x, y);
	m_warpCompensate = true;
	m_warpX = x;
	m_warpY = y;
}