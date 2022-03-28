#include "InputSystem.h"

#include "GraphicsSystem.h"

#include <OgreWindowEventUtilities.h>
#include <SDL.h>
#include <OgreLogManager.h>
#include <filesystem>

namespace
{
Ogre::Matrix4 convertSteamVRMatrixToMatrix4(vr::HmdMatrix34_t matPose)
{
	Ogre::Matrix4 matrixObj(
		matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
		matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3],
		matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], matPose.m[2][3],
		0.0f, 0.0f, 0.0f, 1.0f);
	return matrixObj;
}
}

InputSystem::InputSystem(GraphicsSystem* pGraphics) : m_pGraphics(pGraphics)
{
	if (m_pGraphics->getHMD())
	{
		auto* pVrInput = vr::VRInput();
		std::error_code fs_err;
		std::filesystem::path actionsPath = std::filesystem::absolute("../VRSandbox/Assets/vr/hellovr_actions.json", fs_err);
		vr::EVRInputError vr_err = pVrInput->SetActionManifestPath(actionsPath.string().c_str());
		OGRE_ASSERT(vr_err == vr::EVRInputError::VRInputError_None);

		pVrInput->GetActionSetHandle("/actions/demo", &m_actionsetDemo);

		pVrInput->GetActionHandle("/actions/demo/out/Haptic_Left", &m_actionLeftHaptic);
		pVrInput->GetActionHandle("/actions/demo/in/Hand_Left", &m_actionLeftPose);

		pVrInput->GetActionHandle("/actions/demo/out/Haptic_Right", &m_actionLeftHaptic);
		pVrInput->GetActionHandle("/actions/demo/in/Hand_Right", &m_actionRightPose);

		pVrInput->GetActionHandle("/actions/demo/in/lefthand_anim", &m_actionLeftHandSkeleton);
		pVrInput->GetActionHandle("/actions/demo/in/righthand_anim", &m_actionRightHandSkeleton);
	}
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

	if (m_pGraphics->getHMD())
	{
		updateVRInput();
	}
}

void InputSystem::getVRSkeletalData(HandSkeletonData& outData, vr::VRActionHandle_t skeletonActionHandle)
{
	vr::InputSkeletalActionData_t skelData;
	vr::VRInput()->GetSkeletalActionData(skeletonActionHandle, &skelData, sizeof(skelData));
	if (skelData.bActive)
	{
		m_hasHandSkeletonData = true;
#ifdef OGRE_ASSERTS_ENABLED
		uint32_t boneCount;
		vr::VRInput()->GetBoneCount(skeletonActionHandle, &boneCount);
		OGRE_ASSERT(boneCount == HandSkeletonBone::eBone_Count);
#endif
		{
			vr::VRBoneTransform_t boneTransforms[HandSkeletonBone::eBone_Count];
			vr::VRInput()->GetSkeletalBoneData(skeletonActionHandle, vr::VRSkeletalTransformSpace_Parent,
				vr::VRSkeletalMotionRange_WithoutController, boneTransforms, HandSkeletonBone::eBone_Count);
			for (int i = 0; i < HandSkeletonBone::eBone_Count; ++i)
			{
				outData.boneTransforms[i].bonePos = *reinterpret_cast<Ogre::Vector4*>(&boneTransforms[i].position);
				outData.boneTransforms[i].boneRot = *reinterpret_cast<Ogre::Quaternion*>(&boneTransforms[i].orientation);
			}
		}
	}
	vr::InputPoseActionData_t poseData;
	vr::VRInput()->GetPoseActionDataForNextFrame(skeletonActionHandle, vr::ETrackingUniverseOrigin::TrackingUniverseStanding,
		&poseData, sizeof(poseData), vr::k_ulInvalidInputValueHandle);
	outData.handTransform = convertSteamVRMatrixToMatrix4(poseData.pose.mDeviceToAbsoluteTracking);
}

void InputSystem::updateVRInput()
{
	// Process SteamVR events
	vr::VREvent_t event;
	while (m_pGraphics->getHMD()->PollNextEvent(&event, sizeof(event)))
	{
		switch (event.eventType)
		{
		case vr::VREvent_TrackedDeviceDeactivated:
		{
			printf("Device %u detached.\n", event.trackedDeviceIndex);
		}
		break;
		case vr::VREvent_TrackedDeviceUpdated:
		{
			printf("Device %u updated.\n", event.trackedDeviceIndex);
		}
		break;
		}
	}

	vr::VRActiveActionSet_t actionSet = { 0 };
	actionSet.ulActionSet = m_actionsetDemo;
	vr::VRInput()->UpdateActionState(&actionSet, sizeof(actionSet), 1);

	getVRSkeletalData(m_leftHandSkeletonData, m_actionLeftHandSkeleton);
	getVRSkeletalData(m_rightHandSkeletonData, m_actionRightHandSkeleton);
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