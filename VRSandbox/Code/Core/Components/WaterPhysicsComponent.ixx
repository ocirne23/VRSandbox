module;

#include <btBulletCollisionCommon.h>

export module Components.WaterPhysicsComponent;

export namespace Ogre { class Item; class Vector3; class Quaternion; }

export struct WaterPhysicsComponent
{
	float bouyancy = 6000.0f;
	btVector3 rightingTorque = btVector3(100000.0f, 0, 10000.0f);
};