module;

#include <OgreCamera.h>
#include <OgreWindow.h>
#include <SDL_events.h>

module Utils.CameraFlyingController;

import Systems.GraphicsSystem;
import Systems.InputSystem;
import World;

CameraFlyingController::CameraFlyingController(World& world, Ogre::Window* pWindow, Ogre::Node* pSceneNode, const Ogre::Camera* pCamera, RenderMode renderMode) :
    m_world(world),
    m_pWindow(pWindow),
    m_pNode(pSceneNode),
    m_pCamera(pCamera),
    m_speedModifier(false),
    m_yaw(0),
    m_pitch(0),
    m_camSpeed(10),
    m_boostSpeed(5),
    m_renderMode(renderMode)
{
    memset(m_wasd, 0, sizeof(m_wasd));
    memset(m_upDown, 0, sizeof(m_upDown));
}

CameraFlyingController::~CameraFlyingController()
{
}
//-----------------------------------------------------------------------------------
void CameraFlyingController::update(double timeSinceLast)
{
    m_pNode->setPosition(m_pCamera->getPosition());
    m_pNode->setOrientation(m_pCamera->getOrientation());
    if (m_renderMode != RenderMode::VR && (m_yaw != 0.0f || m_pitch != 0.0f))
    {
        // Update now as yaw needs the derived orientation.
        m_pNode->_getFullTransformUpdated();
        m_pNode->yaw(Ogre::Radian(m_yaw), Ogre::Node::TS_WORLD);
        m_pNode->pitch(Ogre::Radian(m_pitch));

        m_yaw = 0.0f;
        m_pitch = 0.0f;
    }

    int camMovementZ = m_wasd[2] - m_wasd[0];
    int camMovementX = m_wasd[3] - m_wasd[1];
    int slideUpDown = m_upDown[0] - m_upDown[1];

    if (!m_world.getInput().isKeyCurrentlyPressed(SDL_SCANCODE_LALT))
    {
        if (camMovementZ || camMovementX || slideUpDown)
        {
            Ogre::Vector3 camMovementDir((Ogre::Real)camMovementX, 0, (Ogre::Real)camMovementZ);
            camMovementDir.normalise();
            Ogre::Real movementModifier = Ogre::Real(timeSinceLast * m_camSpeed * (1 + m_speedModifier * m_boostSpeed));
            camMovementDir *= movementModifier;
            m_pNode->setPosition(m_pNode->getPosition() + m_pNode->getOrientation() * camMovementDir);
            m_pNode->setPosition(m_pNode->getPosition() + Ogre::Vector3(0, ((Ogre::Real)slideUpDown) * movementModifier, 0));
        }
    }
}
//-----------------------------------------------------------------------------------
bool CameraFlyingController::keyPressed(const SDL_KeyboardEvent& arg)
{
    if (arg.keysym.scancode == SDL_SCANCODE_LSHIFT)
        m_speedModifier = true;

    if (arg.keysym.scancode == SDL_SCANCODE_W)
        m_wasd[0] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_A)
        m_wasd[1] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_S)
        m_wasd[2] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_D)
        m_wasd[3] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_SPACE)
        m_upDown[0] = true;
    else if (arg.keysym.scancode == SDL_SCANCODE_LCTRL)
        m_upDown[1] = true;
    else
        return false;

    return true;
}
//-----------------------------------------------------------------------------------
bool CameraFlyingController::keyReleased(const SDL_KeyboardEvent& arg)
{
    if (arg.keysym.scancode == SDL_SCANCODE_LSHIFT)
        m_speedModifier = false;

    if (arg.keysym.scancode == SDL_SCANCODE_W)
        m_wasd[0] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_A)
        m_wasd[1] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_S)
        m_wasd[2] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_D)
        m_wasd[3] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_SPACE)
        m_upDown[0] = false;
    else if (arg.keysym.scancode == SDL_SCANCODE_LCTRL)
        m_upDown[1] = false;
    else
        return false;

    return true;
}
//-----------------------------------------------------------------------------------
void CameraFlyingController::mouseMoved(const SDL_MouseMotionEvent& arg)
{
    float width = static_cast<float>(m_pWindow->getWidth());
    float height = static_cast<float>(m_pWindow->getHeight());

    m_yaw += -arg.xrel / width;
    m_pitch += -arg.yrel / height;
}