module Utils.MathUtils;

namespace MathUtils
{
Ogre::Vector3 btVectorToOgreVector(const btVector3& btVector)
{
	return Ogre::Vector3(btVector.x(), btVector.y(), btVector.z());
}

btVector3 OgreVectorToBtVector(const Ogre::Vector3& ogreVector)
{
	return btVector3(ogreVector.x, ogreVector.y, ogreVector.z);
}
}