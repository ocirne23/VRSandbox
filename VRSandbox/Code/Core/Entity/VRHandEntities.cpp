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

	void addPhysicsHand(entt::registry& registry, btRigidBody* attachmentBody, PhysicsSystem& physics, SceneSystem& scene, GraphicsSystem& graphics, VRInputSystem& vrInput, EHandType hand)
	{
		entt::entity entity = registry.create();
		auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC);

		float colSize = 0.045f;
		Ogre::Vector3 handColPos = hand == EHandType::LEFT ? Ogre::Vector3(-0.03f, 0.0, 0.12f) : Ogre::Vector3(0.03f, 0.0, 0.12f);

		btCompoundShape* pShape = new btCompoundShape(true, 1);
		btTransform shapePos(btQuaternion::getIdentity(), btVector3(handColPos.x, handColPos.y, handColPos.z));
		pShape->addChildShape(shapePos, physics.createSphereShape(colSize));
		pShape->setMargin(0.5f); // Neccessary for proper physics origin for whatever reason, value doesn't seem to matter.

		auto& physicsComponent = physics.addDynamicPhysicsComponent(registry, entity, pShape, 1.0f);
		physicsComponent.pBody->setActivationState(DISABLE_DEACTIVATION);
		auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, hand == EHandType::LEFT ? "vr_glove_left.mesh" : "vr_glove_right.mesh");
		graphicsComponent.pItem->getParentSceneNode()->setInheritOrientation(false); // Use tracking rotation instead of physics rotation
		auto& handComponent = vrInput.addHandTrackingComponent(registry, entity, hand);
		
		btGeneric6DofSpringConstraint* pSpring = physics.addSpringJointComponent(registry, entity, physicsComponent.pBody, attachmentBody).pSpring;
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
		}
	}

	entt::entity createHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput, EHandType hand)
	{
		const auto entity = registry.create();
		auto& sceneComponent = scene.addSceneNodeComponent(registry, entity, Ogre::SCENE_DYNAMIC);
		auto& handComponent = vrInput.addHandTrackingComponent(registry, entity, hand);
		addHandColliderEntities(registry, handComponent, physics, scene, graphics);
		addPhysicsHand(registry, registry.get<KinematicPhysicsComponent>(handComponent.colliderEntities[0]).pBody, physics, scene, graphics, vrInput, hand);
		// Visualize tracking position with hand mesh or spheres
		//auto& graphicsComponent = graphics.addGraphicsComponent(registry, entity, hand == EHandType::LEFT ? "vr_glove_left.mesh": "vr_glove_right.mesh");
		//addTrackingDebugSpheres(handComponent, graphics);
		return entity;
	}
}