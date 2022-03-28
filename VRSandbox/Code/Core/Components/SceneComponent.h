#pragma once

#include <OgreVector3.h>
#include <OgreQuaternion.h>

namespace Ogre { class SceneNode; }

struct SceneComponent
{
	Ogre::SceneNode* pNode = nullptr;
};