#include "VRHandEntities.h"

#include "Components/GraphicsComponent.h"
#include "Components/PhysicsComponents.h"
#include "Components/VRHandTrackingComponent.h"

#include "Systems/GraphicsSystem.h"
#include "Systems/PhysicsSystem.h"
#include "Systems/SceneSystem.h"
#include "Systems/VRInputSystem.h"

#include <OgreItem.h>
#include <Animation/OgreSkeletonInstance.h>
#include <OgreSceneManager.h>

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>

namespace VRHandEntities
{
	void addDebugSpheres(VRHandTrackingComponent& hand, GraphicsSystem& graphics)
	{
		for (int i = eBone_Wrist; i < eBone_PinkyFinger4; ++i)
		{
			Ogre::Item* item = graphics.getSceneManager()->createItem("Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME, Ogre::SCENE_DYNAMIC);
			item->setDatablock("Marble");
			Ogre::SceneNode* pScaleNode = hand.pArrSceneNodes[i]->createChildSceneNode(Ogre::SCENE_DYNAMIC);
			pScaleNode->scale(Ogre::Vector3(0.02f));
			pScaleNode->attachObject(item);
		}
	}

	entt::entity createLeftHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput)
	{
		const auto entity = registry.create();
		auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC);
		auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "vr_glove_left.mesh");
		auto& handComponent = vrInput.addHandTrackingComponent(registry, entity, VRInputSystem::EHand::LEFT);
		addDebugSpheres(handComponent, graphics);
		return entity;
	}

	entt::entity createRightHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput)
	{
		const auto entity = registry.create();
		auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC);
		auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "vr_glove_right.mesh");
		auto& handComponent = vrInput.addHandTrackingComponent(registry, entity, VRInputSystem::EHand::RIGHT);
		addDebugSpheres(handComponent, graphics);
		return entity;
	}

	/*
	for (int i = 0; i < eBone_PinkyFinger4; ++i)
	{
		btCollisionShape* pShape = pPhysics->createSphereShape(0.04f);
		float mass = i > 0 ? 0.0 :  0.01f;

		btVector3 localInertia(0, 0, 0);
		if (pShape && mass >= 0.0f)
			pShape->calculateLocalInertia(mass, localInertia);
		btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, nullptr, pShape, localInertia);

		hand.pArrHandColliders[i] = new btRigidBody(rbInfo);

		Ogre::Item* item = pGraphics->getSceneManager()->createItem("Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME, Ogre::SCENE_DYNAMIC);
		Ogre::SceneNode* sceneNode = pGraphics->getSceneManager()->getRootSceneNode(Ogre::SCENE_DYNAMIC)->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		item->setDatablock("Marble");
		sceneNode->attachObject(item);

		hand.pArrDebugGraphics[i] = item;
	}

	entt::entity createHandAnchor(entt::registry& registry, Graphics* pGraphics, Physics* pPhysics, entt::entity handEntity)
	{
		const auto entity = registry.create();

		auto& physicsComponent = pPhysics->addPhysicsBodyComponent(registry, entity, nullptr, Ogre::Vector3(1, 1, 0), Ogre::Quaternion::IDENTITY, 0.0f);
		auto& graphicsComponent = pGraphics->addGraphicsComponent(registry, entity, "Sphere1000.mesh", "Marble");
		graphicsComponent.pItem->getParentNode()->setScale(Ogre::Vector3(0.025f, 0.025f, 0.025f));
		
		auto& handPhys = registry.get<PhysicsBodyComponent>(handEntity);

		
		btGeneric6DofSpring2Constraint* pSpring = new btGeneric6DofSpring2Constraint(*physicsComponent.pBody, *handPhys.pBody,
			btTransform(btQuaternion::getIdentity(), { 0.0f, -0.05f, -0.1f }),
			btTransform(btQuaternion::getIdentity(), { 0.0f, -0.05f, -0.1f }));

		for (int i = 0; i < 3; ++i)
		{
			pSpring->enableSpring(i, true);
			pSpring->setStiffness(i, 100.0f);
			pSpring->setDamping(i, 0.001f);
			//pSpring->setLimit(i, 0, 1);
			//pSpring->setDamping(i, 0.01f);
		}

		for (int i = 3; i < 6; ++i)
		{
			pSpring->enableSpring(i, true);
			pSpring->setLimit(i, 0, 0);
		}

		//pSpring->setBreakingImpulseThreshold(FLT_MAX);

		pPhysics->getWorld()->addConstraint(pSpring);

		return entity;
	}
	*/
}