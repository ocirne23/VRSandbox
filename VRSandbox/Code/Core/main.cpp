#include <OgrePrerequisites.h>
#include <OgreTimer.h>
#include <OgreWindow.h>
#include <Threading/OgreThreads.h>
#include <OgreLogManager.h>
#include <entt/entity/registry.hpp>
#include <OgreCamera.h>

import Systems.GraphicsSystem;
import Systems.PhysicsSystem;
import Systems.InputSystem;
import Systems.SceneSystem;
import Systems.VRInputSystem;

import Utils.CameraController;
import Utils.OpenVRCompositorListener;
import Utils.PathUtils;

import TestWorld;

const double FIXED_TIMESTEP_TIME = 1.0 / 60.0;

const RenderMode RENDER_MODE = RenderMode::VR;

int main(int argc, const char *argv[])
{
    PathUtils::setHardcodedWorkingDir();

    entt::registry registry;
    PhysicsSystem physics;
    GraphicsSystem graphics("test", RENDER_MODE);
    
    InputSystem input(&graphics);
    input.setWantMouseGrab(true);
    input.setWantMouseRelative(true);
    input.setWantMouseVisible(false);

    VRInputSystem vrInput(graphics.getHMD(), graphics);

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

    TestWorld testWorld(registry, graphics, physics, scene, vrInput);
    testWorld.createScene();

    Ogre::Timer timer;
    Ogre::uint64 startTime = timer.getMicroseconds();
    double deltaSec = 1.0 / 60.0;
    const double PHYSICS_TIMESTEP = 1.0 / 60.0;
    while (!input.hasQuit())
    {
        physics.update(deltaSec, PHYSICS_TIMESTEP, registry);
        input.update(deltaSec, registry);
        vrInput.update(deltaSec, registry);
        //cameraController.update(timeSinceLast);
        graphics.update(deltaSec, registry);

        testWorld.update(deltaSec);

        if (!graphics.getRenderWindow()->isVisible())
            Ogre::Threads::Sleep(500);

        Ogre::uint64 endTime = timer.getMicroseconds();
        deltaSec = (endTime - startTime) / 1'000'000.0;
        deltaSec = std::min(1.0, deltaSec); //Prevent from going haywire.
        startTime = endTime;
    }

    input.destroyMouseListener(pMouseListener);
    input.destroyKeyboardListener(pKeyboardListener);
    
    return 0;
}
