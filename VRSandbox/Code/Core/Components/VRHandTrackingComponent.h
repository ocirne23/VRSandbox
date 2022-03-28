#pragma once

#include "Utils/EHandSkeletonBone.h"

class btRigidBody;
namespace Ogre { class Item; class SceneNode; }

struct VRHandTrackingComponent
{
	int trackingID = 0;
	btRigidBody* pWristCollider;
	btRigidBody* pFingerTipColliders[5];
	Ogre::SceneNode* pArrSceneNodes[eBone_PinkyFinger4];
};