module;

#include <OgreSceneManager.h>
#include <OgreItem.h>
#include <OgreMeshManager.h>
#include <OgreMeshManager2.h>
#include <OgreMesh2.h>
#include <OgreCamera.h>
#include <OgreWindow.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreHlmsSamplerblock.h>
#include <OgreRoot.h>
#include <OgreHlmsManager.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureFilters.h>
#include <OgreHlmsPbs.h>
#include <OgreLogManager.h>
#include <Animation/OgreSkeletonInstance.h>

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>
#include <SDL_scancode.h>
#include <SDL_events.h>

module TestScene;

import <memory>;

import Components.VRHandTrackingComponent;

import Components.GraphicsComponent;
import Components.DynamicPhysicsComponent;
import Components.SceneComponent;
import Components.BoatControlComponent;

import Systems.GraphicsSystem;
import Systems.InputSystem;
import Systems.PhysicsSystem;
import Systems.VRInputSystem;

import Utils.CameraFlyingController;
import Utils.DebugDrawer;

import Entity.TestEntities;
import Entity.VRHandEntities;

import World;

extern const bool c_useRDM;

// Set this to false to disable Radial Density Mask (RDM) optimization
// The value is hardcoded in C++, however you can extend this to be
// controlled at runtime
//
// The main reason we allow this setting to be disabled is because it is
// causing glitches in NVIDIA GPUs in Linux, see https://github.com/OGRECave/ogre-next/issues/53
const bool c_useRDM = true;

TestScene::TestScene(World& world, entt::registry& registry) : m_world(world), m_registry(registry)
{
}

TestScene::~TestScene()
{
}

void TestScene::createScene()
{
    TestEntities::createTestFloorEntity(m_world);
#if 0
    {
    const Ogre::Vector3 minSpawn(-3, 5, -3);
    const Ogre::Vector3 maxSpawn(3, 10, 3);
    for (int i = 0; i < 10; ++i) TestEntities::createTestCube   (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestSphere (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestCone   (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestCapsule(m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestRamp   (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestWedge  (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    }
#endif
    m_waterEntity = TestEntities::createTestWater(m_world, Ogre::Vector3(0, 0, 0));

    m_boatEntity = TestEntities::createTestBoat(m_world, Ogre::Vector3(0, 15, 0));

    TestEntities::createTestBoatNPC(m_world, Ogre::Vector3(10, 15, 0));


    const Ogre::Vector3 minSpawn(-20, 100, -20);
    const Ogre::Vector3 maxSpawn(20, 5000, 20);

    //for (int i = 0; i < 1000; ++i) TestEntities::createTestSphere(m_world, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));

    Ogre::SceneManager* pSceneManager = m_world.getGraphics().getSceneManager();

    Ogre::SceneNode* rootNode = pSceneManager->getRootSceneNode();

    Ogre::Light* light = pSceneManager->createLight();
    Ogre::SceneNode* lightNode = rootNode->createChildSceneNode();
    lightNode->attachObject(light);
    light->setPowerScale(1.0f);
    light->setType(Ogre::Light::LT_DIRECTIONAL);
    light->setDirection(Ogre::Vector3(-1, -1, -1).normalisedCopy());

    m_lightNodes[0] = lightNode;

    pSceneManager->setAmbientLight(Ogre::ColourValue(0.3f, 0.5f, 0.7f) * 0.1f * 0.75f, Ogre::ColourValue(0.6f, 0.45f, 0.3f) * 0.065f * 0.75f,
        -light->getDirection() + Ogre::Vector3::UNIT_Y * 0.2f);

    light = pSceneManager->createLight();
    lightNode = rootNode->createChildSceneNode();
    lightNode->attachObject(light);
    light->setDiffuseColour(0.8f, 0.4f, 0.2f); //Warm
    light->setSpecularColour(0.8f, 0.4f, 0.2f);
    light->setPowerScale(Ogre::Math::PI);
    light->setType(Ogre::Light::LT_SPOTLIGHT);
    lightNode->setPosition(-10.0f, 50.0f, 10.0f);
    light->setDirection(Ogre::Vector3(1, -1, -1).normalisedCopy());
    light->setAttenuationBasedOnRadius(60.0f, 0.01f);
    
    m_lightNodes[1] = lightNode;
    /*
    light = pSceneManager->createLight();
    lightNode = rootNode->createChildSceneNode();
    lightNode->attachObject(light);
    light->setDiffuseColour(0.2f, 0.4f, 0.8f); //Cold
    light->setSpecularColour(0.2f, 0.4f, 0.8f);
    light->setPowerScale(Ogre::Math::PI);
    light->setType(Ogre::Light::LT_SPOTLIGHT);
    lightNode->setPosition(10.0f, 10.0f, -10.0f);
    light->setDirection(Ogre::Vector3(-1, -1, 1).normalisedCopy());
    light->setAttenuationBasedOnRadius(10.0f, 0.01f);

    m_lightNodes[2] = lightNode;*/
    setupControls();
}

float wave = 0.0f;
void TestScene::update(double deltaSec)
{
    auto& input = m_world.getInput();
    auto& boatCtrl = m_world.getRegistry().get<BoatControlComponent>(m_boatEntity);

    if (input.isKeyCurrentlyPressed(SDL_SCANCODE_LALT))
    {
        bool w = input.isKeyCurrentlyPressed(SDL_SCANCODE_W);
        bool a = input.isKeyCurrentlyPressed(SDL_SCANCODE_A);
        bool s = input.isKeyCurrentlyPressed(SDL_SCANCODE_S);
        bool d = input.isKeyCurrentlyPressed(SDL_SCANCODE_D);

        boatCtrl.forwardBack = w ? 1.0f : (s ? -0.3f : 0.0f);
        boatCtrl.rightLeft = d ? 1.0f : (a ? -1.0f : 0.0f);
    }
    else
    {
		boatCtrl.forwardBack = 0.0f;
		boatCtrl.rightLeft = 0.0f;
	}

    if (m_world.getGraphics().getRenderMode() == RenderMode::Desktop)
        m_pCameraFlyingController->update(deltaSec);

    /*
    Ogre::SceneNode* pCameraNode = m_world.getGraphics().getCameraNode();
    pCameraNode->_getDerivedPositionUpdated(); // to avoid crash in lookat
    pCameraNode->lookAt(m_registry.get<SceneComponent>(m_boatEntity).pNode->getPosition(), Ogre::Node::TS_WORLD);*/
}

void TestScene::setupControls()
{
    auto& graphics = m_world.getGraphics();
    m_pCameraFlyingController = std::make_unique<CameraFlyingController>(m_world,
        graphics.getRenderWindow(), graphics.getCameraNode(),
        graphics.getCamera(), graphics.getRenderMode());

    Ogre::SceneNode* pCameraNode = graphics.getCameraNode();
    pCameraNode->setPosition(Ogre::Vector3(0, 70, 50));
    pCameraNode->_getDerivedPositionUpdated(); // to avoid crash in lookat
    pCameraNode->lookAt(Ogre::Vector3(0, 15, 0), Ogre::Node::TS_WORLD);

    auto& boatCtrl = m_world.getRegistry().get<BoatControlComponent>(m_boatEntity);

    MouseListener* pMouseListener = m_world.getInput().createMouseListener();
    pMouseListener->onMouseMoved = [&](const SDL_MouseMotionEvent& e)
        {
            m_pCameraFlyingController->mouseMoved(e);
        };

    KeyboardListener* pKeyboardListener = m_world.getInput().createKeyboardListener();
    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& e)
        {
            m_pCameraFlyingController->keyPressed(e);
        };
    pKeyboardListener->onKeyReleased = [&](const SDL_KeyboardEvent& e)
        {
            m_pCameraFlyingController->keyReleased(e);
        };
}

/*
    if (m_handEntities[0] == entt::null && m_vrInput.isHandTrackingActive(EHandType::LEFT))
        m_handEntities[0] = VRHandEntities::createHand(m_registry, m_graphics, m_physics, m_scene, m_vrInput, EHandType::LEFT);
    if (m_handEntities[1] == entt::null && m_vrInput.isHandTrackingActive(EHandType::RIGHT))
        m_handEntities[1] = VRHandEntities::createHand(m_registry, m_graphics, m_physics, m_scene, m_vrInput, EHandType::RIGHT);
*/