#pragma once

#include "Systems/InputSystem.h"

class btRigidBody;
namespace Ogre { class Item; class SceneNode; }

struct VRHandTrackingComponent
{
	btRigidBody* pWristCollider;
	btRigidBody* pFingerTipColliders[5];
	Ogre::SceneNode* pArrSceneNodes[eBone_PinkyFinger4];
};