module;

#include "Utils/ArraySize.h"

#include <OgreItem.h>
#include <OgreSceneNode.h>
#include <Animation/OgreSkeletonInstance.h>
#include <OgreSceneManager.h>

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>

module Entity.VRHandEntities;

import Components.VRHandTrackingComponent;

import Components.GraphicsComponent;
import Components.SceneComponent;
import Components.KinematicPhysicsComponent;
import Components.DynamicPhysicsComponent;
import Components.SpringJointComponent;

import Systems.GraphicsSystem;
import Systems.PhysicsSystem;
import Systems.SceneSystem;
import Systems.VRInputSystem;

import Utils.EHandSkeletonBone;
import World;


namespace VRHandEntities
{
	void addTrackingDebugSpheres(VRHandTrackingComponent& hand, GraphicsSystem& graphics)
	{
		for (int i = (int)EHandSkeletonBone::Wrist; i < (int)EHandSkeletonBone::PinkyFinger4; ++i)
		{
			Ogre::Item* item = graphics.getSceneManager()->createItem("Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME, Ogre::SCENE_DYNAMIC);
			item->setDatablock("Marble");
			Ogre::SceneNode* pScaleNode = hand.pArrSceneNodes[i]->createChildSceneNode(Ogre::SCENE_DYNAMIC);
			pScaleNode->scale(Ogre::Vector3(0.02f));
			pScaleNode->attachObject(item);
		}
	}

	void addPhysicsHand(World& world, btRigidBody* attachmentBody, EHandType hand)
	{
		entt::entity entity = world.getRegistry().create();
		auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC);

		float colSize = 0.045f;
		Ogre::Vector3 handColPos = hand == EHandType::LEFT ? Ogre::Vector3(-0.03f, 0.0, 0.12f) : Ogre::Vector3(0.03f, 0.0, 0.12f);

		btCompoundShape* pShape = new btCompoundShape(true, 1);
		btTransform shapePos(btQuaternion::getIdentity(), btVector3(handColPos.x, handColPos.y, handColPos.z));
		pShape->addChildShape(shapePos, world.getPhysics().createSphereShape(colSize));
		pShape->setMargin(0.5f); // Neccessary for proper physics origin for whatever reason, value doesn't seem to matter.

		auto& physicsComponent = world.getPhysics().addDynamicPhysicsComponent(entity, pShape, 1.0f);
		physicsComponent.pBody->setActivationState(DISABLE_DEACTIVATION);
		auto& graphicsComponent = world.getGraphics().addGraphicsComponent(entity, hand == EHandType::LEFT ? "vr_glove_left.mesh" : "vr_glove_right.mesh");
		graphicsComponent.pItem->getParentSceneNode()->setInheritOrientation(false); // Use tracking rotation instead of physics rotation
		auto& handComponent = world.getVRInput().addHandTrackingComponent(entity, hand);
		
		btGeneric6DofSpring2Constraint* pSpring = world.getPhysics().addSpringJointComponent(entity, physicsComponent.pBody, attachmentBody).pSpring;
		for (int i = 0; i < 3; ++i)
		{
			pSpring->enableSpring(i, true);
			pSpring->setStiffness(i, 0.05f);
			pSpring->setDamping(i, 0.1f);
		}
		for (int i = 3; i < 6; ++i)
			pSpring->enableSpring(i, false);
		pSpring->setAngularLowerLimit(btVector3(0, 0, 0));
		pSpring->setAngularUpperLimit(btVector3(0, 0, 0));

		// Visualize physics colider
#if 0
		Ogre::Item* item = graphics.getSceneManager()->createItem("Sphere1000.mesh", Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME, Ogre::SCENE_DYNAMIC);
		item->setDatablock("Marble");
		Ogre::SceneNode* pScaleNode = sceneComponent.pNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		pScaleNode->setPosition(handColPos);
		pScaleNode->scale(Ogre::Vector3(colSize * 2.0f));
		pScaleNode->attachObject(item);
#endif
	}

	void addHandColliderEntities(World& world, VRHandTrackingComponent& hand)
	{
		EHandSkeletonBone handBones[] = { EHandSkeletonBone::Root, EHandSkeletonBone::Thumb3, 
			EHandSkeletonBone::IndexFinger4, EHandSkeletonBone::MiddleFinger4, 
			EHandSkeletonBone::RingFinger4, EHandSkeletonBone::PinkyFinger4 };
		static_assert(ARRAY_SIZE(hand.colliderEntities) == ARRAY_SIZE(handBones));

		int colliderEntityIdx = 0;
		for (EHandSkeletonBone bone : handBones)
		{
			entt::entity entity = world.getRegistry().create();
			hand.colliderEntities[colliderEntityIdx++] = entity;
			SceneComponent& sceneComp = world.getScene().addSceneNodeComponentWithParent(entity, hand.pArrSceneNodes[(int)bone]);
			world.getPhysics().addKinematicPhysicsComponent(entity, new btEmptyShape());
		}
	}

	entt::entity createHand(World& world, EHandType hand)
	{
		entt::registry& registry = world.getRegistry();
		const auto entity = registry.create();
		auto& sceneComponent = world.getScene().addSceneNodeComponent(entity, Ogre::SCENE_DYNAMIC);
		auto& handComponent = world.getVRInput().addHandTrackingComponent(entity, hand);
		addHandColliderEntities(world, handComponent);
		addPhysicsHand(world, registry.get<KinematicPhysicsComponent>(handComponent.colliderEntities[0]).pBody, hand);
		// Visualize tracking position with hand mesh or spheres
		//auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, hand == EHandType::LEFT ? "vr_glove_left.mesh": "vr_glove_right.mesh");
		//addTrackingDebugSpheres(handComponent, graphics);
		return entity;
	}
}