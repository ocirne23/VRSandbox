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

import Components.GraphicsComponent;
import Components.SceneComponent;
import Components.KinematicPhysicsComponent;
import Components.DynamicPhysicsComponent;
import Components.StaticPhysicsComponent;

import Systems.GraphicsSystem;
import Systems.PhysicsSystem;
import Systems.SceneSystem;

namespace TestEntities
{

entt::entity createTestFloorEntity(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene)
{
    // Create generated plane mesh
    static bool once = true;
    if (once)
    {
        once = false;

        Ogre::v1::MeshPtr planeMeshV1 = Ogre::v1::MeshManager::getSingleton().createPlane("Plane v1",
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::Plane(Ogre::Vector3::UNIT_Y, 0.0f), 500.0f, 500.0f,
            1, 1, true, 1, 4.0f, 4.0f, Ogre::Vector3::UNIT_Z, Ogre::v1::HardwareBuffer::HBU_STATIC, Ogre::v1::HardwareBuffer::HBU_STATIC);

        Ogre::MeshPtr planeMesh = Ogre::MeshManager::getSingleton().createByImportingV1("Plane", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            planeMeshV1.get(), true, true, true);
    }

    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_STATIC, Ogre::Vector3(0, -1, 0), Ogre::Quaternion::IDENTITY);
    auto& physicsComponent = physics.addStaticPhysicsComponent(registry, entity, physics.createBoxShape(Ogre::Vector3(250, 1, 250)));
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);

    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Plane", "Marble");
    graphics.setGraphicsOffset(registry, entity, Ogre::Vector3(0, 1, 0));

    {   //TODO: cleanup the datablock stuff below
        Ogre::HlmsPbsDatablock* datablock = static_cast<Ogre::HlmsPbsDatablock*>(graphicsComponent.pItem->getSubItem(0)->getDatablock());
        Ogre::HlmsSamplerblock samplerblock(*datablock->getSamplerblock(Ogre::PBSM_ROUGHNESS));
        samplerblock.mU = Ogre::TAM_WRAP;
        samplerblock.mV = Ogre::TAM_WRAP;
        samplerblock.mW = Ogre::TAM_WRAP;
        datablock->setSamplerblock(Ogre::PBSM_ROUGHNESS, samplerblock);
    }
    
	return entity;
}

entt::entity createTestSphere(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos)
{
    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Sphere.mesh");
    DynamicPhysicsComponent& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, physics.createSphereShape(1.0f), 1000.0f);
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.2f);
    physicsComponent.pBody->setSleepingThresholds(0.001, 0.001);
    return entity;
}

entt::entity createTestCube(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos)
{
    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Cube.mesh");
    auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, physics.createBoxShape(Ogre::Vector3(1,1,1)), 1000.0f);
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestCone(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos)
{
    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Cone.mesh");
    auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, physics.createConeShape(1.0f, 1.0f), 1000.0f);
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestCapsule(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos)
{
    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Capsule.mesh");
    auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, physics.createCapsuleShape(1.0f, 1.0f), 1000.0f);
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestRamp(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos)
{
    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Ramp.mesh");
    auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, physics.createTriangleShapeFromOgreMesh("Ramp.mesh"), 1000.0f);
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

entt::entity createTestWedge(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos)
{
    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC, pos, Ogre::Quaternion::IDENTITY);
    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Wedge.mesh");
    auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, physics.createTriangleShapeFromOgreMesh("Wedge.mesh"), 1000.0f);
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    physicsComponent.pBody->setDamping(0.0f, 0.5f);
    return entity;
}

}