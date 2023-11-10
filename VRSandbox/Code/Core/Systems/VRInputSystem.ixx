module;

#include <entt/fwd.hpp>

export module Systems.VRInputSystem;

import Utils.HandSkeletonData;
import Utils.EHandType;

export class World;
export struct VRHandTrackingComponent;
export class GraphicsSystem;

export namespace vr
{
    class IVRSystem; typedef uint64_t VRActionHandle_t; typedef uint64_t VRActionSetHandle_t; struct InputAnalogActionData_t; typedef uint64_t VRInputValueHandle_t;
}

export class VRInputSystem
{
public:

	VRInputSystem(World& world, entt::registry& registry);
	virtual ~VRInputSystem();
	VRInputSystem(const VRInputSystem& copy) = delete;

    void initialize(vr::IVRSystem* pIVRSystem, GraphicsSystem* pGraphics);

	void update(double deltaSec);

    VRHandTrackingComponent& addHandTrackingComponent(entt::entity entity, EHandType handType);
    void removeHandTrackingComponent(entt::entity entity);

    bool isHandTrackingActive(EHandType hand);

private:

    bool getVRSkeletalData(HandSkeletonData& outData, vr::VRActionHandle_t skeletonActionHandle);
    vr::InputAnalogActionData_t getAnalogDataForHand(EHandType hand);

private:

    World& m_world;
    entt::registry& m_registry;

	vr::IVRSystem* m_pIVRSystem = nullptr;
    GraphicsSystem* m_pGraphics;

    vr::VRActionSetHandle_t m_actionsetDemo = 0;

    vr::VRInputValueHandle_t m_valueHandleRightHand = 0;
    vr::VRInputValueHandle_t m_valueHandleLeftHand = 0;

    vr::VRActionHandle_t m_actionLeftThumbStick = 0;
    vr::VRActionHandle_t m_actionRightThumbStick = 0;
    vr::VRActionHandle_t m_actionLeftPose = 0;
    vr::VRActionHandle_t m_actionRightPose = 0;
    vr::VRActionHandle_t m_actionLeftHaptic = 0;
    vr::VRActionHandle_t m_actionRightHaptic = 0;
    vr::VRActionHandle_t m_actionLeftHandSkeleton = 0;
    vr::VRActionHandle_t m_actionRightHandSkeleton = 0;

    HandSkeletonData m_leftHandSkeletonData;
    HandSkeletonData m_rightHandSkeletonData;

    bool m_isHandTrackingActive[(int)EHandType::COUNT] = { false, false };
};