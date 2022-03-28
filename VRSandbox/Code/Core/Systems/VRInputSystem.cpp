#include "VRInputSystem.h"

#include "Components/VRHandTrackingComponent.h"
#include "Components/GraphicsComponent.h"
#include "Components/SceneComponent.h"

#include "Systems/GraphicsSystem.h"
#include "Utils/VRMathUtils.h"

#include <OgreSceneManager.h>
#include <OgreItem.h>
#include <Animation/OgreSkeletonInstance.h>

#include <entt/entity/registry.hpp>
#include <openvr.h>
#include <filesystem>

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
		std::filesystem::path actionsPath = std::filesystem::absolute("../VRSandbox/Assets/vr/hellovr_actions.json", fs_err);
		vr::EVRInputError vr_err = pVrInput->SetActionManifestPath(actionsPath.string().c_str());
		OGRE_ASSERT(vr_err == vr::EVRInputError::VRInputError_None);

		pVrInput->GetActionSetHandle("/actions/demo", &m_actionsetDemo);

		pVrInput->GetActionHandle("/actions/demo/out/Haptic_Left", &m_actionLeftHaptic);
		pVrInput->GetActionHandle("/actions/demo/in/Hand_Left", &m_actionLeftPose);

		pVrInput->GetActionHandle("/actions/demo/out/Haptic_Right", &m_actionLeftHaptic);
		pVrInput->GetActionHandle("/actions/demo/in/Hand_Right", &m_actionRightPose);

		pVrInput->GetActionHandle("/actions/demo/in/lefthand_anim", &m_actionLeftHandSkeleton);
		pVrInput->GetActionHandle("/actions/demo/in/righthand_anim", &m_actionRightHandSkeleton);
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

		m_isHandTrackingActive[EHand::LEFT] = getVRSkeletalData(m_leftHandSkeletonData, m_actionLeftHandSkeleton);
		m_isHandTrackingActive[EHand::RIGHT] = getVRSkeletalData(m_rightHandSkeletonData, m_actionRightHandSkeleton);
	}

	auto updateHandNodes = registry.view<VRHandTrackingComponent>();
	updateHandNodes.each([&](VRHandTrackingComponent& handComp)
		{
			if (handComp.trackingID >= EHand::COUNT || !m_isHandTrackingActive[handComp.trackingID])
				return;

			HandSkeletonData& handData = handComp.trackingID == EHand::LEFT ? m_leftHandSkeletonData : m_rightHandSkeletonData;

			for (int i = 0; i < eBone_PinkyFinger4; ++i)
			{
				auto& trans = handData.boneTransforms[i];
				handComp.pArrSceneNodes[i]->setPosition(trans.bonePos.xyz());
				handComp.pArrSceneNodes[i]->setOrientation(trans.boneRot);
			}
		});

	auto updateHandPosition = registry.view<const VRHandTrackingComponent, SceneComponent>();
	updateHandPosition.each([&](const VRHandTrackingComponent& handComp, SceneComponent& sceneComp)
		{
			if (handComp.trackingID >= EHand::COUNT || !m_isHandTrackingActive[handComp.trackingID])
				return;
			HandSkeletonData& handData = handComp.trackingID == EHand::LEFT ? m_leftHandSkeletonData : m_rightHandSkeletonData;

			sceneComp.pNode->setPosition(handData.handTransform.getTrans());
			sceneComp.pNode->setOrientation(handData.handTransform.extractQuaternion());
		});

	auto updateHandGraphics = registry.view<const VRHandTrackingComponent, GraphicsComponent>();
	updateHandGraphics.each([&](const VRHandTrackingComponent& handComp, GraphicsComponent& graphicsComp)
		{
			if (handComp.trackingID >= EHand::COUNT || !m_isHandTrackingActive[handComp.trackingID])
				return;
			HandSkeletonData& handData = handComp.trackingID == EHand::LEFT ? m_leftHandSkeletonData : m_rightHandSkeletonData;

			auto* skelInstance = graphicsComp.pItem->getSkeletonInstance();
			for (int i = 0; i < eBone_Count; ++i)
			{
				auto* pBone = skelInstance->getBone(i);
				pBone->setOrientation(handData.boneTransforms[i].boneRot);
				pBone->setPosition(handData.boneTransforms[i].bonePos.xyz());
			}
		});
}


VRHandTrackingComponent& VRInputSystem::addHandTrackingComponent(entt::registry& registry, entt::entity entity, int trackingID)
{
	OGRE_ASSERT(trackingID < EHand::COUNT); // TODO: more than 2 controllers for multiplayer?

	VRHandTrackingComponent& hand = registry.emplace<VRHandTrackingComponent>(entity);
	hand.trackingID = trackingID;

	Ogre::SceneNode* handRootNode = m_graphics.getSceneManager()->getRootSceneNode(Ogre::SCENE_DYNAMIC)->createChildSceneNode(Ogre::SCENE_DYNAMIC);
	hand.pArrSceneNodes[eBone_Root] = handRootNode;
	{
		Ogre::SceneNode* wristNode = handRootNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		hand.pArrSceneNodes[eBone_Wrist] = wristNode;
		Ogre::SceneNode* parentNode = wristNode;
		for (int i = eBone_Thumb0; i <= eBone_Thumb3; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = eBone_IndexFinger0; i <= eBone_IndexFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = eBone_MiddleFinger0; i <= eBone_MiddleFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = eBone_RingFinger0; i <= eBone_RingFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		parentNode = wristNode;
		for (int i = eBone_PinkyFinger0; i <= eBone_PinkyFinger4; ++i)
			hand.pArrSceneNodes[i] = parentNode = parentNode->createChildSceneNode(Ogre::SCENE_DYNAMIC);
	}
	return hand;
}

void VRInputSystem::removeHandTrackingComponent(entt::registry& registry, entt::entity entity)
{
	VRHandTrackingComponent& hand = registry.get<VRHandTrackingComponent>(entity);
	m_graphics.getSceneManager()->getRootSceneNode(Ogre::SCENE_DYNAMIC)->removeChild(hand.pArrSceneNodes[eBone_Root]);
	registry.erase<VRHandTrackingComponent>(entity);
}

bool VRInputSystem::isHandTrackingActive(EHand hand)
{
	return m_isHandTrackingActive[hand];
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
		OGRE_ASSERT(boneCount == EHandSkeletonBone::eBone_Count);
#endif
		{
			vr::VRBoneTransform_t boneTransforms[EHandSkeletonBone::eBone_Count];
			vr::VRInput()->GetSkeletalBoneData(skeletonActionHandle, vr::VRSkeletalTransformSpace_Parent,
				vr::VRSkeletalMotionRange_WithoutController, boneTransforms, EHandSkeletonBone::eBone_Count);
			for (int i = 0; i < EHandSkeletonBone::eBone_Count; ++i)
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
