module;

#include <OgreCamera.h>
#include <OgreWindow.h>
#include <SDL_events.h>

module Utils.CameraController;

import Systems.GraphicsSystem;

CameraController::CameraController(Ogre::Window* pWindow, Ogre::Node* pSceneNode, const Ogre::Camera* pCamera, RenderMode renderMode) :
    m_pWindow(pWindow),
    m_pNode(pSceneNode),
    m_pCamera(pCamera),
    mSpeedMofifier(false),
    mCameraYaw(0),
    mCameraPitch(0),
    mCameraBaseSpeed(10),
    mCameraSpeedBoost(5),
    m_renderMode(renderMode)
{
    memset(mWASD, 0, sizeof(mWASD));
    memset(mSlideUpDown, 0, sizeof(mSlideUpDown));
}

CameraController::~CameraController()
{
}
//-----------------------------------------------------------------------------------
void CameraController::update(double timeSinceLast)
{
    m_pNode->setPosition(m_pCamera->getPosition());
    m_pNode->setOrientation(m_pCamera->getOrientation());
    if (m_renderMode != RenderMode::VR && (mCameraYaw != 0.0f || mCameraPitch != 0.0f))
    {
        // Update now as yaw needs the derived orientation.
        m_pNode->_getFullTransformUpdated();
        m_pNode->yaw(Ogre::Radian(mCameraYaw), Ogre::Node::TS_WORLD);
        m_pNode->pitch(Ogre::Radian(mCameraPitch));

        mCameraYaw = 0.0f;
        mCameraPitch = 0.0f;
    }

    int camMovementZ = mWASD[2] - mWASD[0];
    int camMovementX = mWASD[3] - mWASD[1];
    int slideUpDown = mSlideUpDown[0] - mSlideUpDown[1];

    if (camMovementZ || camMovementX || slideUpDown)
    {
        Ogre::Vector3 camMovementDir((Ogre::Real)camMovementX, 0, (Ogre::Real)camMovementZ);
        camMovementDir.normalise();
        Ogre::Real movementModifier = Ogre::Real(timeSinceLast * mCameraBaseSpeed * (1 + mSpeedMofifier * mCameraSpeedBoost));
        camMovementDir *= movementModifier;
        m_pNode->setPosition(m_pNode->getPosition() + m_pNode->getOrientation() * camMovementDir);
        m_pNode->setPosition(m_pNode->getPosition() + Ogre::Vector3(0, ((Ogre::Real)slideUpDown) * movementModifier, 0));
    }
}
//-----------------------------------------------------------------------------------
bool CameraController::keyPressed(const SDL_KeyboardEvent& arg)
{
    if (arg.keysym.scancode == SDL_SCANCODE_LSHIFT)
        mSpeedMofifier = true;

    if (arg.keysym.scancode == SDL_SCANCODE_W)
        mWASD[0] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_A)
        mWASD[1] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_S)
        mWASD[2] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_D)
        mWASD[3] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_SPACE)
        mSlideUpDown[0] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_LCTRL)
        mSlideUpDown[1] = true;
    else
        return false;

    return true;
}
//-----------------------------------------------------------------------------------
bool CameraController::keyReleased(const SDL_KeyboardEvent& arg)
{
    if (arg.keysym.scancode == SDL_SCANCODE_LSHIFT)
        mSpeedMofifier = false;

    if (arg.keysym.scancode == SDL_SCANCODE_W)
        mWASD[0] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_A)
        mWASD[1] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_S)
        mWASD[2] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_D)
        mWASD[3] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_SPACE)
        mSlideUpDown[0] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_LCTRL)
        mSlideUpDown[1] = false;
    else
        return false;

    return true;
}
//-----------------------------------------------------------------------------------
void CameraController::mouseMoved(const SDL_MouseMotionEvent& arg)
{
    float width = static_cast<float>(m_pWindow->getWidth());
    float height = static_cast<float>(m_pWindow->getHeight());

    mCameraYaw += -arg.xrel / width;
    mCameraPitch += -arg.yrel / height;
}