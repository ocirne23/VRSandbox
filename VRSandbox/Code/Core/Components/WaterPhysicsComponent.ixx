module;

#include <btBulletCollisionCommon.h>

export module Components.WaterPhysicsComponent;

export namespace Ogre { class Item; class Vector3; class Quaternion; }

export struct WaterPhysicsComponent
{
	float bouyancyForce = 1.0f;
};