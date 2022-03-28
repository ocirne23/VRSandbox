#pragma once

#include "Utils/HandSkeletonData.h"

#include <entt/fwd.hpp>

struct VRHandTrackingComponent;
class GraphicsSystem;

namespace vr { class IVRSystem; typedef uint64_t VRActionHandle_t; typedef uint64_t VRActionSetHandle_t; }

class VRInputSystem
{
public:

	VRInputSystem(vr::IVRSystem* pIVRSystem, GraphicsSystem& graphics);
	virtual ~VRInputSystem();
	VRInputSystem(const VRInputSystem& copy) = delete;

	void update(double deltaSec, entt::registry& registry);

    VRHandTrackingComponent& addHandTrackingComponent(entt::registry& registry, entt::entity entity, int trackingID);
    void removeHandTrackingComponent(entt::registry& registry, entt::entity entity);

    enum EHand
    {
        LEFT = 0,
        RIGHT,
        COUNT
    };

    bool isHandTrackingActive(EHand hand);

private:

    bool getVRSkeletalData(HandSkeletonData& outData, vr::VRActionHandle_t skeletonActionHandle);

private:

	vr::IVRSystem* m_pIVRSystem = nullptr;
    GraphicsSystem& m_graphics;

    vr::VRActionSetHandle_t m_actionsetDemo = 0;

    vr::VRActionHandle_t m_actionLeftPose = 0;
    vr::VRActionHandle_t m_actionRightPose = 0;
    vr::VRActionHandle_t m_actionLeftHaptic = 0;
    vr::VRActionHandle_t m_actionRightHaptic = 0;
    vr::VRActionHandle_t m_actionLeftHandSkeleton = 0;
    vr::VRActionHandle_t m_actionRightHandSkeleton = 0;

    HandSkeletonData m_leftHandSkeletonData;
    HandSkeletonData m_rightHandSkeletonData;

    bool m_isHandTrackingActive[EHand::COUNT] = { false, false };
};