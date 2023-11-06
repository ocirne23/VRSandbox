module;

#include <OgreMatrix4.h>
#include <OgreVector4.h>
#include <OgreQuaternion.h>

export module Utils.HandSkeletonData;
export import Utils.EHandSkeletonBone;

export struct HandSkeletonData
{
    Ogre::Matrix4 handTransform = Ogre::Matrix4::IDENTITY;
    struct
    {
        Ogre::Vector4 bonePos = {};
        Ogre::Quaternion boneRot = Ogre::Quaternion();
    } boneTransforms[(int)EHandSkeletonBone::Count] = {};
};