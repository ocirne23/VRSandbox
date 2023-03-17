module;

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreItem.h>
#include <Animation/OgreSkeletonInstance.h>

#include <entt/entity/registry.hpp>
#include <openvr.h>
#include <filesystem>

module Systems.VRInputSystem;

import Systems.GraphicsSystem;

import Components.GraphicsComponent;
import Components.SceneComponent;
import Components.DynamicPhysicsComponent;
import Components.VRHandTrackingComponent;

import Utils.VRMathUtils;

VRInputSystem::VRInputSystem(vr::IVRSystem* pIVRSystem, GraphicsSystem& graphics) :
	m_pIVRSystem(pIVRSystem),
	m_graphics(graphics)
{
	memset(&m_leftHandSkeletonData, 0, sizeof(m_leftHandSkeletonData));
	memset(&m_rightHandSkeletonData, 0, sizeof(m_rightHandSkeletonData));

	if (m_pIVRSystem)
	{
		auto* pVrInput = vr::VRInput();
		std::error_code fs_err;
		std::filesystem::path actionsPath = std::filesystem::absolute("C:/Github/VRSandbox/VRSandbox/Assets/vr/hellovr_actions.json", fs_err);
		vr::EVRInputError vr_err = pVrInput->SetActionManifestPath(actionsPath.string().c_str());
		OGRE_ASSERT(vr_err == vr::EVRInputError::VRInputError_None);

		pVrInput->GetActionSetHandle("/actions/demo", &m_actionsetDemo);

		pVrInput->GetActionHandle("/actions/demo/out/Haptic_Left", &m_actionLeftHaptic);
		pVrInput->GetActionHandle("/actions/demo/in/Hand_Left", &m_actionLeftPose);

		pVrInput->GetActionHandle("/actions/demo/out/Haptic_Right", &m_actionLeftHaptic);
		pVrInput->GetActionHandle("/actions/demo/in/Hand_Right", &m_actionRightPose);

		pVrInput->GetActionHandle("/actions/demo/in/lefthand_anim", &m_actionLeftHandSkeleton);
		pVrInput->GetActionHandle("/actions/demo/in/righthand_anim", &m_actionRightHandSkeleton);

		// TODO: fix strings
		pVrInput->GetInputSourceHandle("user/hand/left", &m_valueHandleLeftHand);
		pVrInput->GetInputSourceHandle("user/hand/right", &m_valueHandleRightHand);
	}
}

VRInputSystem::~VRInputSystem()
{

}

void VRInputSystem::update(double deltaSec, entt::registry& registry)
{
	if (m_pIVRSystem)
	{
		vr::VREvent_t event;
		while (m_pIVRSystem->PollNextEvent(&event, sizeof(event)))
		{
			switch (event.eventType)
			{
			case vr::VREvent_TrackedDeviceDeactivated:
			{
				printf("Device %u detached.\n", event.trackedDeviceIndex);
			}
			break;
			case vr::VREvent_TrackedDeviceUpdated:
			{
				printf("Device %u updated.\n", event.trackedDeviceIndex);
			}
			break;
			}
		}

		vr::VRActiveActionSet_t actionSet = { 0 };
		actionSet.ulActionSet = m_actionsetDemo;
		vr::VRInput()->UpdateActionState(&actionSet, sizeof(actionSet), 1);

		m_isHandTrackingActive[(int)EHandType::LEFT] = getVRSkeletalData(m_leftHandSkeletonData, m_actionLeftHandSkeleton);
		m_isHandTrackingActive[(int)EHandType::RIGHT] = getVRSkeletalData(m_rightHandSkeletonData, m_actionRightHandSkeleton);
	}

	auto updateHandNodes = registry.view<VRHandTrackingComponent>();
	updateHandNodes.each([&](VRHandTrackingComponent& handComp)
		{
			if ((int)handComp.handType >= (int)EHandType::COUNT || !isHandTrackingActive(handComp.handType))
				return;
			const HandSkeletonData& handData = handComp.handType == EHandType::LEFT ? m_leftHandSkeletonData : m_rightHandSkeletonData;

			for (int i = 0; i < (int)EHandSkeletonBone::PinkyFinger4; ++i)
			{
				handComp.pArrSceneNodes[i]->setPosition(handData.boneTransforms[i].bonePos.xyz());
				handComp.pArrSceneNodes[i]->setOrientation(handData.boneTransforms[i].boneRot);
			}
		});
	
	auto updateHandTransformNoPhysics = registry.view<const VRHandTrackingComponent, SceneComponent>(entt::exclude<DynamicPhysicsComponent>);
	updateHandTransformNoPhysics.each([&](const VRHandTrackingComponent& handComp, SceneComponent& sceneComp)
		{
			if (handComp.handType >= EHandType::COUNT || !isHandTrackingActive(handComp.handType))
				return;
			const HandSkeletonData& handData = handComp.handType == EHandType::LEFT ? m_leftHandSkeletonData : m_rightHandSkeletonData;

			sceneComp.pNode->setPosition(handData.handTransform.getTrans());
			sceneComp.pNode->setOrientation(handData.handTransform.extractQuaternion());
		});
	
	auto updatePhysicsHandRotation = registry.view<const VRHandTrackingComponent, GraphicsComponent, DynamicPhysicsComponent>();
	updatePhysicsHandRotation.each([&](const VRHandTrackingComponent& handComp, GraphicsComponent& grapComp, DynamicPhysicsComponent& physComp)
		{
			HandSkeletonData& handData = handComp.handType == EHandType::LEFT ? m_leftHandSkeletonData : m_rightHandSkeletonData;
			m_graphics.setGraphicsRotation(grapComp, handData.handTransform.extractQuaternion());
		});
		
	auto updateHandGraphics = registry.view<const VRHandTrackingComponent, GraphicsComponent>();
	updateHandGraphics.each([&](const VRHandTrackingComponent& handComp, GraphicsComponent& graphicsComp)
		{
			if (handComp.handType >= EHandType::COUNT || !isHandTrackingActive(handComp.handType))
				return;
			HandSkeletonData& handData = handComp.handType == EHandType::LEFT ? m_leftHandSkeletonData : m_rightHandSkeletonData;
			
			auto* skelInstance = graphicsComp.pItem->getSkeletonInstance();
			for (int i = 0; i < (int)EHandSkeletonBone::PinkyFinger4; ++i)
			{
				auto* pBone = skelInstance->getBone(i);
				pBone->setPosition(handData.boneTransforms[i].bonePos.xyz());
				pBone->setOrientation(handData.boneTransforms[i].boneRot);
			}
		});
}

VRHandTrackingComponent& VRInputSystem::addHandTrackingComponent(entt::registry& registry, entt::entity entity, EHandType handType)
{
	OGRE_ASSERT(handType < EHandType::COUNT); // TODO: more than 2 controllers for multiplayer?

	SceneComponent& sceneComponent = registry.get<SceneComponent>(entity);
	VRHandTrackingComponent& hand = registry.emplace<VRHandTrackingComponent>(entity);
	hand.handType = handType;

	Ogre::SceneNode* handRootNode = sceneComponent.pNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
	hand.pArrSceneNodes[(int)EHandSkeletonBone::Root] = handRootNode;
	{
		Ogre::SceneNode* wristNode = handRootNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		hand.pArrSceneNodes[(int)EHandSkeletonBone::Wrist] = wristNode;
		Ogre::SceneNode* parentNode = wristNode;
		for (int i = (int)EHandSkeletonBone::Thumb0; i <= (int)EHandSkeletonBone::Thumb3; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = (int)EHandSkeletonBone::IndexFinger0; i <= (int)EHandSkeletonBone::IndexFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = (int)EHandSkeletonBone::MiddleFinger0; i <= (int)EHandSkeletonBone::MiddleFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = (int)EHandSkeletonBone::RingFinger0; i <= (int)EHandSkeletonBone::RingFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = (int)EHandSkeletonBone::PinkyFinger0; i <= (int)EHandSkeletonBone::PinkyFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
	}
	return hand;
}

void VRInputSystem::removeHandTrackingComponent(entt::registry& registry, entt::entity entity)
{
	VRHandTrackingComponent& hand = registry.get<VRHandTrackingComponent>(entity);
	m_graphics.getSceneManager()->getRootSceneNode(Ogre::SCENE_DYNAMIC)->removeChild(hand.pArrSceneNodes[(int)EHandSkeletonBone::Root]);
	registry.erase<VRHandTrackingComponent>(entity);
}

bool VRInputSystem::isHandTrackingActive(EHandType hand)
{
	return m_isHandTrackingActive[(int)hand];
}

bool VRInputSystem::getVRSkeletalData(HandSkeletonData& outData, vr::VRActionHandle_t skeletonActionHandle)
{
	vr::InputSkeletalActionData_t skelData;
	vr::VRInput()->GetSkeletalActionData(skeletonActionHandle, &skelData, sizeof(skelData));
	if (skelData.bActive)
	{
#ifdef OGRE_ASSERTS_ENABLED
		uint32_t boneCount;
		vr::VRInput()->GetBoneCount(skeletonActionHandle, &boneCount);
		OGRE_ASSERT(boneCount == (int)EHandSkeletonBone::Count);
#endif
		{
			vr::VRBoneTransform_t boneTransforms[(int)EHandSkeletonBone::Count];
			vr::VRInput()->GetSkeletalBoneData(skeletonActionHandle, vr::VRSkeletalTransformSpace_Parent,
				vr::VRSkeletalMotionRange_WithoutController, boneTransforms, (int)EHandSkeletonBone::Count);
			for (int i = 0; i < (int)EHandSkeletonBone::Count; ++i)
			{
				outData.boneTransforms[i].bonePos = *reinterpret_cast<Ogre::Vector4*>(&boneTransforms[i].position);
				outData.boneTransforms[i].boneRot = *reinterpret_cast<Ogre::Quaternion*>(&boneTransforms[i].orientation);
			}
		}
	}
	vr::InputPoseActionData_t poseData;
	vr::VRInput()->GetPoseActionDataForNextFrame(skeletonActionHandle, vr::ETrackingUniverseOrigin::TrackingUniverseStanding,
		&poseData, sizeof(poseData), vr::k_ulInvalidInputValueHandle);
	outData.handTransform = VRMathUtils::convertSteamVRMatrixToMatrix4(poseData.pose.mDeviceToAbsoluteTracking);

	return skelData.bActive;
}

vr::InputAnalogActionData_t VRInputSystem::getAnalogDataForHand(EHandType hand)
{
	vr::VRActionHandle_t actionHandle = hand == EHandType::LEFT ? m_actionLeftThumbStick : m_actionRightThumbStick;
	vr::VRInputValueHandle_t valueHandle = hand == EHandType::LEFT ? m_valueHandleLeftHand : m_valueHandleRightHand;

	vr::InputAnalogActionData_t analogData;
	vr::EVRInputError err = vr::VRInput()->GetAnalogActionData(actionHandle, &analogData, sizeof(analogData), valueHandle);
	OGRE_ASSERT(err == vr::VRInputError_None);

	return analogData;
}