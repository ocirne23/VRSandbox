module;

#include <OgreVector3.h>

export module Components.BoatControlComponent;

export struct BoatControlComponent
{
	float forwardBack = 0.0f; // 1.0 fwd, -1.0 bwd
	float rightLeft = 0.0f;   // 1.0 right, -1.0 left
	Ogre::Vector3 aimPoint;
	bool fire = false;

	float moveSpeedMultiplier = 3.0f;
	float rotationSpeedMultiplier = 1.5f;
};