module;

#include <OgreVector3.h>
#include <btBulletDynamicsCommon.h>

export module Utils.MathUtils;

export namespace MathUtils
{
	Ogre::Vector3 btVectorToOgreVector(const btVector3& btVector);
	btVector3 OgreVectorToBtVector(const Ogre::Vector3& ogreVector);
}