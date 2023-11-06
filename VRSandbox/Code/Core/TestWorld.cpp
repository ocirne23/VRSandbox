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

module TestWorld;

import Components.VRHandTrackingComponent;

import Components.GraphicsComponent;
import Components.DynamicPhysicsComponent;
import Components.SceneComponent;

import Systems.GraphicsSystem;
import Systems.InputSystem;
import Systems.PhysicsSystem;
import Systems.VRInputSystem;

import Utils.CameraController;
import Utils.DebugDrawer;

import Entity.TestEntities;
import Entity.VRHandEntities;

extern const bool c_useRDM;

// Set this to false to disable Radial Density Mask (RDM) optimization
// The value is hardcoded in C++, however you can extend this to be
// controlled at runtime
//
// The main reason we allow this setting to be disabled is because it is
// causing glitches in NVIDIA GPUs in Linux, see https://github.com/OGRECave/ogre-next/issues/53
const bool c_useRDM = true;

TestWorld::TestWorld(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene,
    VRInputSystem& vrInput) :
    m_registry(registry),
    m_graphics(graphics),
    m_physics(physics),
    m_scene(scene),
    m_vrInput(vrInput)
{
}

TestWorld::~TestWorld()
{
}

void TestWorld::createScene()
{
    TestEntities::createTestFloorEntity(m_registry, m_graphics, m_physics, m_scene);
#if 1
    const Ogre::Vector3 minSpawn(-3, 5, -3);
    const Ogre::Vector3 maxSpawn(3, 10, 3);
    for (int i = 0; i < 10; ++i) TestEntities::createTestCube   (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestSphere (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestCone   (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestCapsule(m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestRamp   (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
    for (int i = 0; i < 10; ++i) TestEntities::createTestWedge  (m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(minSpawn.x, maxSpawn.x), Ogre::Math::RangeRandom(minSpawn.y, maxSpawn.y), Ogre::Math::RangeRandom(minSpawn.z, maxSpawn.z)));
#else
    for (int i = 0; i < 5000; ++i)
        TestEntities::createTestSphere(m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(Ogre::Math::RangeRandom(-5, 5), Ogre::Math::RangeRandom(20, 100), Ogre::Math::RangeRandom(-5, 5)));
#endif
    entt::entity e1 = TestEntities::createTestSphere(m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(2, 30, 0));
    entt::entity e2 = TestEntities::createTestSphere(m_registry, m_graphics, m_physics, m_scene, Ogre::Vector3(3, 30, 0));

    auto c1 = m_registry.get<DynamicPhysicsComponent>(e1);
    auto c2 = m_registry.get<DynamicPhysicsComponent>(e2);
    // m_physics.addFixedJointComponent(m_registry, e1, c1.pBody, c2.pBody, { 0, 0, 0.5}, { 0, 0, -0.5 });
    m_physics.addSpringJointComponent(m_registry, e1, c1.pBody, c2.pBody);


    Ogre::SceneManager* pSceneManager = m_graphics.getSceneManager();

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
    lightNode->setPosition(-10.0f, 10.0f, 10.0f);
    light->setDirection(Ogre::Vector3(1, -1, -1).normalisedCopy());
    light->setAttenuationBasedOnRadius(10.0f, 0.01f);
    
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
}

void TestWorld::update(double deltaSec)
{
    if (m_handEntities[0] == entt::null && m_vrInput.isHandTrackingActive(EHandType::LEFT))
        m_handEntities[0] = VRHandEntities::createHand(m_registry, m_graphics, m_physics, m_scene, m_vrInput, EHandType::LEFT);
    if (m_handEntities[1] == entt::null && m_vrInput.isHandTrackingActive(EHandType::RIGHT))
        m_handEntities[1] = VRHandEntities::createHand(m_registry, m_graphics, m_physics, m_scene, m_vrInput, EHandType::RIGHT);
}