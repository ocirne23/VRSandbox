module;

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>

#include <OgreSceneManager.h>
#include <OgreItem.h>
#include <OgreMeshManager.h>
#include <OgreMeshManager2.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreHlmsSamplerblock.h>

module Entity.TestEntities;

import Components.SceneComponent;
import Components.KinematicPhysicsComponent;
import Components.StaticPhysicsComponent;
import Components.DynamicPhysicsComponent;
import Components.GraphicsComponent;
import Components.WaterPhysicsComponent;

import Systems.GraphicsSystem;
import Systems.PhysicsSystem;
import Systems.SceneSystem;

import World;

namespace TestEntities
{

entt::entity createTestFloorEntity(World& world)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_STATIC, Ogre::Vector3(0, 0, 0), Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Plane.mesh");
    auto& physicsComponent = world.getPhysics().addStaticPhysicsComponent(entity, world.getPhysics().createBoxShape(Ogre::Vector3(250, 1, 250)));
    
    graphicsComponent.setScale(Ogre::Vector3(250, 1, 250));
    graphicsComponent.setOffset(Ogre::Vector3(0, 1, 0));

    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.2f);
    return entity;
}

entt::entity createTestSphere(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Sphere.mesh");
    DynamicPhysicsComponent& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, world.getPhysics().createSphereShape(1.0f), 100.0f);
    
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.2f, 0.2f);
    //physicsComponent.pBody->setSleepingThresholds(0.001, 0.001);
    return entity;
}

entt::entity createTestCube(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Cube.mesh");
    auto& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, world.getPhysics().createBoxShape(Ogre::Vector3(1, 1, 1)), 1000.0f);
   
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestCone(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Cone.mesh");
    auto& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, world.getPhysics().createConeShape(1.0f, 1.0f), 1000.0f);
    
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestCapsule(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Capsule.mesh");
    auto& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, world.getPhysics().createCapsuleShape(1.0f, 1.0f), 1000.0f);
    
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestRamp(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Ramp.mesh");
    auto& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, world.getPhysics().createTriangleShapeFromOgreMesh("Ramp.mesh"), 1000.0f);
    
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestWedge(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Wedge.mesh");
    auto& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, world.getPhysics().createTriangleShapeFromOgreMesh("Wedge.mesh"), 1000.0f);
    
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestBoat(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Boat.mesh");
    auto& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, world.getPhysics().createTriangleShapeFromOgreMesh("Boat.mesh", Ogre::Vector3(2, 2, 2)), 1000.0f);
    auto& waterPhysicsComponent = world.getWaterPhysics().addWaterPhysicsComponent(entity);
    auto& boatControlComponent = world.getBoatControl().addBoatControlComponent(entity);

    graphicsComponent.setScale(Ogre::Vector3(2, 2, 2));

    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.5f, 0.75f);
    physicsComponent.pBody->setSleepingThresholds(0.0, 0.0);
    return entity;
}

entt::entity createTestWater(World& world, const Ogre::Vector3& pos)
{
    const auto entity = world.getRegistry().create();
    auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, "Plane.mesh", "Water");
    
    graphicsComponent.setScale(Ogre::Vector3(500, 1, 500));
    return entity;
}

}