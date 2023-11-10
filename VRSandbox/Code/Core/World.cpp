module;

#include <OgreCamera.h>
#include <OgreTimer.h>
#include <Threading/OgreThreads.h>
#include <entt/fwd.hpp>

module World;

import TestScene;
import Utils.CameraController;

World::World(entt::registry& registry) 
    : m_registry(registry)
    , m_graphics(*this, registry)
    , m_physics(*this, registry)
    , m_scene(*this, registry)
    , m_vrInput(*this, registry)
    , m_input(*this, registry)
    , m_waterPhysics(*this, registry)
{
}

World::~World() {}

void World::initialize()
{
    m_graphics.initialize("TestWorld", RenderMode::Desktop);
    m_scene.initialize(&m_graphics);

    m_input.initialize(&m_graphics);
    m_input.setWantMouseGrab(true);
    m_input.setWantMouseRelative(true);
    m_input.setWantMouseVisible(false);

    m_vrInput.initialize(m_graphics.getHMD(), &m_graphics);

    m_physics.initialize();
    m_physics.setDebugDrawer(m_graphics.getDebugDrawer());
    //physics.setEnableDebugDraw(true);

    m_waterPhysics.initialize();

    setupCamera();

    m_pTestScene = std::make_unique<TestScene>(*this);
    m_pTestScene->createScene();
}

void World::setupCamera()
{
    Ogre::Camera* pCamera = m_graphics.getCamera();
    pCamera->setPosition(Ogre::Vector3(15, 15, 15));
    pCamera->lookAt(Ogre::Vector3(0, 0, 0));

    m_pCameraController = std::make_unique<CameraController>(
            m_graphics.getRenderWindow(), m_graphics.getCameraNode(),
            m_graphics.getCamera(), m_graphics.getRenderMode());

    MouseListener* pMouseListener = m_input.createMouseListener();
    pMouseListener->onMouseMoved = [&](const SDL_MouseMotionEvent& e)
        {
            m_pCameraController->mouseMoved(e);
        };

    KeyboardListener* pKeyboardListener = m_input.createKeyboardListener();
    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& e)
        {
            m_pCameraController->keyPressed(e);
        };
    pKeyboardListener->onKeyReleased = [&](const SDL_KeyboardEvent& e)
        {
            m_pCameraController->keyReleased(e);
        };
}

void World::updateLoop()
{
    Ogre::Timer timer;
    Ogre::uint64 startTime = timer.getMicroseconds();
    double deltaSec = 1.0 / 60.0;
    const double PHYSICS_TIMESTEP = 1.0 / 60.0;
    while (!m_wantsShutdown && !m_input.hasQuit())
    {
        m_physics.update(deltaSec, PHYSICS_TIMESTEP);
        m_waterPhysics.update(deltaSec);

        m_input.update(deltaSec);
        m_vrInput.update(deltaSec);

        if (m_graphics.getRenderMode() == RenderMode::Desktop)
            m_pCameraController->update(deltaSec);

        m_graphics.update(deltaSec);

        m_pTestScene->update(deltaSec);

        if (!m_graphics.isWindowFocused())
            Ogre::Threads::Sleep(500);

        Ogre::uint64 endTime = timer.getMicroseconds();
        deltaSec = std::min(1.0, (endTime - startTime) / 1'000'000.0); // Prevent from going haywire.
        startTime = endTime;
    }
}
