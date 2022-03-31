#include "TestEntities.h"

#include "Components/GraphicsComponent.h"
#include "Components/DynamicPhysicsComponent.h"
#include "Components/StaticPhysicsComponent.h"
#include "Components/KinematicPhysicsComponent.h"
#include "Components/SceneComponent.h"

#include "Systems/GraphicsSystem.h"
#include "Systems/PhysicsSystem.h"
#include "Systems/SceneSystem.h"

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>

#include <OgreSceneManager.h>
#include <OgreItem.h>
#include <OgreMeshManager.h>
#include <OgreMeshManager2.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreHlmsSamplerblock.h>

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
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::Plane(Ogre::Vector3::UNIT_Y, 0.0f), 50.0f, 50.0f,
            1, 1, true, 1, 4.0f, 4.0f, Ogre::Vector3::UNIT_Z, Ogre::v1::HardwareBuffer::HBU_STATIC, Ogre::v1::HardwareBuffer::HBU_STATIC);

        Ogre::MeshPtr planeMesh = Ogre::MeshManager::getSingleton().createByImportingV1("Plane", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            planeMeshV1.get(), true, true, true);
    }

    const auto entity = registry.create();
    auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_STATIC, Ogre::Vector3(0, -1, 0), Ogre::Quaternion::IDENTITY);
    auto& physicsComponent = physics.addStaticPhysicsComponent(registry, entity, physics.createBoxShape(Ogre::Vector3(25, 1, 25)));
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
    auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Sphere1000.mesh", "Marble");
    auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, physics.createSphereShape(0.5f), 100.0f);
    physicsComponent.pBody->setFriction(0.5f);
    physicsComponent.pBody->setRollingFriction(0.5f);
    physicsComponent.pBody->setSpinningFriction(0.5f);
    return entity;
}

}