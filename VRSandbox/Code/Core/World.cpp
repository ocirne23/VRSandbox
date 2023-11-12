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
    //m_physics.setEnableDebugDraw(true);

    m_waterPhysics.initialize();

    m_boatControl.initialize();

    m_pTestScene = std::make_unique<TestScene>(*this, m_registry);
    m_pTestScene->createScene();
}


void World::updateLoop()
{
    Ogre::Timer timer;
    Ogre::uint64 startTime = timer.getMicroseconds();
    double deltaSec = 1.0 / 60.0;
    const double PHYSICS_FIXED_TIMESTEP = 1.0 / 20.0;
    while (!m_wantsShutdown && !m_input.hasQuit())
    {
        m_physics.update(deltaSec, PHYSICS_FIXED_TIMESTEP);
        m_waterPhysics.update(deltaSec);
        m_boatControl.update(deltaSec);

        m_input.update(deltaSec);
        m_vrInput.update(deltaSec);
        m_graphics.update(deltaSec);

        m_pTestScene->update(deltaSec);

        Ogre::uint64 endTime = timer.getMicroseconds();
        deltaSec = std::min(1.0, (endTime - startTime) / 1'000'000.0); // Prevent from going haywire.
        startTime = endTime;
    }
}
