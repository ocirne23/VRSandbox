module;

#include <entt/fwd.hpp>
#include <OgreVector3.h>

export module Components.VRHandTrackingComponent;

import Utils.EHandSkeletonBone;
import Utils.EHandType;

export namespace Ogre { class Item; class SceneNode; }

export struct VRHandTrackingComponent
{
	EHandType handType = EHandType::LEFT;
	entt::entity colliderEntities[6];
	Ogre::SceneNode* pArrSceneNodes[(int)EHandSkeletonBone::PinkyFinger4];
};