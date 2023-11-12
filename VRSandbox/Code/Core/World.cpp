module;

#include <OgreCamera.h>
#include <OgreTimer.h>
#include <OgreLogManager.h>
#include <OgreStringConverter.h>
#include <Threading/OgreThreads.h>
#include <entt/fwd.hpp>

module World;

import TestScene;

World::World(entt::registry& registry) 
    : m_registry(registry)
    , m_graphics(*this, registry)
    , m_physics(*this, registry)
    , m_scene(*this, registry)
    , m_vrInput(*this, registry)
    , m_input(*this, registry)
    , m_waterPhysics(*this, registry)
    , m_boatControl(*this, registry)
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

    m_boatControl.initialize();

    setupCamera();

    m_pTestScene = std::make_unique<TestScene>(*this);
    m_pTestScene->createScene();
}

void World::setupCamera()
{
    Ogre::Camera* pCamera = m_graphics.getCamera();
    pCamera->setPosition(Ogre::Vector3(30, 30, 30));
    pCamera->lookAt(Ogre::Vector3(0, 0, 0));
}

void World::updateLoop()
{
    Ogre::Timer timer;
    Ogre::uint64 startTime = timer.getMicroseconds();
    double deltaSec = 1.0 / 60.0;
    double timeAccumulator = 0.0;
    const double PHYSICS_TIMESTEP = 1.0 / 60.0;
    while (!m_wantsShutdown && !m_input.hasQuit())
    {
        timeAccumulator += deltaSec;
        while (timeAccumulator >= PHYSICS_TIMESTEP)
        {
            timeAccumulator -= PHYSICS_TIMESTEP;
            m_physics.update(PHYSICS_TIMESTEP, PHYSICS_TIMESTEP);
            m_waterPhysics.update(PHYSICS_TIMESTEP);
            if (timeAccumulator > 1.0f)
            {
                Ogre::LogManager::getSingleton().logMessage("Physics is running too slow!");
                timeAccumulator = 0.0f;
            }
        }

        m_input.update(deltaSec);
        m_vrInput.update(deltaSec);

        m_graphics.update(deltaSec);
        m_boatControl.update(deltaSec);

        m_pTestScene->update(deltaSec);

        Ogre::uint64 endTime = timer.getMicroseconds();
        /*
        if (!m_graphics.isWindowFocused())
        {
            uint64_t ms = 100 - (endTime - startTime) / 1'000;
            if (ms > 0)
                Ogre::Threads::Sleep((int)ms);
        }*/

        deltaSec = std::min(1.0, (endTime - startTime) / 1'000'000.0); // Prevent from going haywire.
        startTime = endTime;
    }
}
