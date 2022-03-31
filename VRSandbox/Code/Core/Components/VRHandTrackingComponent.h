#pragma once

#include "Utils/EHandSkeletonBone.h"
#include "Utils/EHandType.h"
#include <entt/fwd.hpp>
#include <OgreVector3.h>

namespace Ogre { class Item; class SceneNode; }

struct VRHandTrackingComponent
{
	EHandType handType = EHandType::LEFT;
	entt::entity colliderEntities[6];
	Ogre::SceneNode* pArrSceneNodes[(int)EHandSkeletonBone::PinkyFinger4];
};