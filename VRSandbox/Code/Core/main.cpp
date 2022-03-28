#include "Systems/GraphicsSystem.h"
#include "Systems/PhysicsSystem.h"
#include "Systems/InputSystem.h"
#include "Systems/SceneSystem.h"

#include "TestWorld.h"
#include "Utils/CameraController.h"
#include "Utils/OpenVRCompositorListener.h"
#include "Utils/PathUtils.h"

#include <OgrePrerequisites.h>
#include <OgreTimer.h>
#include <OgreWindow.h>
#include <Threading/OgreThreads.h>
#include <OgreLogManager.h>
#include <entt/entity/registry.hpp>

const double FIXED_TIMESTEP_TIME = 1.0 / 60.0;

int main(int argc, const char *argv[])
{
    PathUtils::setHardcodedWorkingDir();
    entt::registry registry;

    PhysicsSystem physics;

    GraphicsSystem graphics("test", RenderMode::VR);
    InputSystem input(&graphics);
    input.setWantMouseGrab(true);
    input.setWantMouseRelative(true);
    input.setWantMouseVisible(false);

    SceneSystem scene(&graphics);

    Ogre::Camera* pCamera = graphics.getCamera();
    pCamera->setPosition(Ogre::Vector3(0, 0, 1));
    pCamera->lookAt(Ogre::Vector3(0, 0, 0));

    CameraController cameraController(graphics.getRenderWindow(), graphics.getControllerNode(), 
        graphics.getCamera(), graphics.getRenderMode());
    MouseListener* pMouseListener = input.createMouseListener();
    pMouseListener->onMouseMoved = [&](const SDL_MouseMotionEvent& e)
    {
        cameraController.mouseMoved(e);
    };

    KeyboardListener* pKeyboardListener = input.createKeyboardListener();
    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& e)
    {
        cameraController.keyPressed(e);
    };
    pKeyboardListener->onKeyReleased = [&](const SDL_KeyboardEvent& e)
    {
        cameraController.keyReleased(e);
    };

    TestWorld testWorld(registry, graphics, physics, scene);
    testWorld.createScene();

    Ogre::Timer timer;
    Ogre::uint64 startTime = timer.getMicroseconds();
    double timeSinceLast = 1.0 / 60.0;
    const double PHYSICS_TIMESTEP = 1.0 / 60.0;
    while (!input.hasQuit())
    {
        physics.update(timeSinceLast, PHYSICS_TIMESTEP, registry);

        input.update(timeSinceLast, registry);
        //cameraController.update(timeSinceLast);

        if (input.hasHandSkeletonData())
            testWorld.setVRHandTrackingData(input.getLeftHandSkeletonData(), input.getRightHandSkeletonData());

        graphics.update(timeSinceLast, registry);

        if (!graphics.getRenderWindow()->isVisible())
        {
            Ogre::Threads::Sleep(500);
        }

        Ogre::uint64 endTime = timer.getMicroseconds();
        timeSinceLast = (endTime - startTime) / 1'000'000.0;
        timeSinceLast = std::min(1.0, timeSinceLast); //Prevent from going haywire.
        startTime = endTime;
    }

    input.destroyMouseListener(pMouseListener);
    input.destroyKeyboardListener(pKeyboardListener);

    return 0;
}
