#include "VRHandEntities.h"

#include "Components/GraphicsComponent.h"
#include "Components/VRHandTrackingComponent.h"
#include "Components/SceneComponent.h"
#include "Components/KinematicPhysicsComponent.h"
#include "Components/DynamicPhysicsComponent.h"
#include "Components/SpringJointComponent.h"

#include "Systems/GraphicsSystem.h"
#include "Systems/PhysicsSystem.h"
#include "Systems/SceneSystem.h"
#include "Systems/VRInputSystem.h"

#include "Utils/ArraySize.h"

#include <OgreItem.h>
#include <OgreSceneNode.h>
#include <Animation/OgreSkeletonInstance.h>
#include <OgreSceneManager.h>

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>

namespace VRHandEntities
{
	void addDebugSpheres(VRHandTrackingComponent& hand, GraphicsSystem& graphics)
	{
		for (int i = 0; i < (int)EHandSkeletonBone::PinkyFinger4; ++i)
		{
			Ogre::Item* item = graphics.getSceneManager()->createItem("Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME, Ogre::SCENE_DYNAMIC);
			item->setDatablock("Marble");
			Ogre::SceneNode* pScaleNode = hand.pArrSceneNodes[i]->createChildSceneNode(Ogre::SCENE_DYNAMIC);
			pScaleNode->scale(Ogre::Vector3(0.02f));
			pScaleNode->attachObject(item);
		}
	}

	void addPhysicsHand(entt::registry& registry, btRigidBody* attachmentBody, PhysicsSystem& physics, SceneSystem& scene, GraphicsSystem& graphics, VRInputSystem& vrInput, EHandType hand)
	{
		entt::entity entity = registry.create();
		auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC);

		float colSize = 0.045f;
		Ogre::Vector3 handColPos(0, 0, 0);
		Ogre::Quaternion ori = sceneComponent.pNode->getOrientation();
		btCompoundShape* pShape = new btCompoundShape(true, 1);
		btTransform shapePos(btQuaternion(ori.x, ori.y, ori.z, ori.w), btVector3(handColPos.x, handColPos.y, handColPos.z));
		pShape->addChildShape(shapePos, physics.createSphereShape(colSize));
		//pShape->createAabbTreeFromChildren();
		pShape->setMargin(0.5f);

		auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, pShape, 2.0f);
		physicsComponent.pBody->setActivationState(DISABLE_DEACTIVATION);
		auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, hand == EHandType::LEFT ? "vr_glove_left.mesh" : "vr_glove_right.mesh");
		//graphicsComponent.pItem->getParentNode()->setOrientation(Ogre::Quaternion::IDENTITY);
		auto& handComponent = vrInput.addHandTrackingComponent(registry, entity, hand);

		//physicsComponent.pBody->setContactStiffnessAndDamping(0, 0);
		//physicsComponent.pBody->setRollingFriction(0);
		Ogre::Item* item = graphics.getSceneManager()->createItem("Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME, Ogre::SCENE_DYNAMIC);
		item->setDatablock("Marble");
		Ogre::SceneNode* pScaleNode = sceneComponent.pNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		pScaleNode->setPosition(handColPos);
		pScaleNode->scale(Ogre::Vector3(colSize * 2.0f));
		pScaleNode->attachObject(item);

		auto& springComponent = physics.addSpringJointComponent(registry, entity, physicsComponent.pBody, attachmentBody);
	}

	void addHandColliderEntities(entt::registry& registry, VRHandTrackingComponent& hand, PhysicsSystem& physics, SceneSystem& scene, GraphicsSystem& graphics)
	{
		EHandSkeletonBone handBones[] = { EHandSkeletonBone::Root, EHandSkeletonBone::Thumb3, 
			EHandSkeletonBone::IndexFinger4, EHandSkeletonBone::MiddleFinger4, 
			EHandSkeletonBone::RingFinger4, EHandSkeletonBone::PinkyFinger4 };
		static_assert(ARRAY_SIZE(hand.colliderEntities) == ARRAY_SIZE(handBones));

		int colliderEntityIdx = 0;
		for (EHandSkeletonBone bone : handBones)
		{
			entt::entity entity = registry.create();
			hand.colliderEntities[colliderEntityIdx++] = entity;

			SceneComponent& sceneComp = scene.addSceneNodeComponentWithParent(registry, entity, hand.pArrSceneNodes[(int)bone]);
			physics.addKinematicPhysicsComponent(registry, entity, new btEmptyShape());
			auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, "Sphere1000.mesh", "Marble");
			graphics.setGraphicsScale(graphicsComponent, Ogre::Vector3(0.02f));

			// Manually fix offsets on wrist and pinky colliders
			if (bone == EHandSkeletonBone::Root)
			{
				graphics.setGraphicsScale(graphicsComponent, Ogre::Vector3(0.01f));
			}
			else if (bone == EHandSkeletonBone::PinkyFinger4)
			{
				sceneComp.pNode->setPosition(Ogre::Vector3(hand.handType == EHandType::LEFT ? 0.02f : -0.02f, 0, 0));
			}
			sceneComp.pNode->_updateChildren();
		}
	}
	entt::entity createHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput, EHandType hand)
	{
		const auto entity = registry.create();
		auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC);
		auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, hand == EHandType::LEFT ? "vr_glove_left.mesh": "vr_glove_right.mesh");
		auto& handComponent = vrInput.addHandTrackingComponent(registry, entity, hand);
		addDebugSpheres(handComponent, graphics);
		addHandColliderEntities(registry, handComponent, physics, scene, graphics);
		addPhysicsHand(registry, registry.get<KinematicPhysicsComponent>(handComponent.colliderEntities[0]).pBody, physics, scene, graphics, vrInput, hand);
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