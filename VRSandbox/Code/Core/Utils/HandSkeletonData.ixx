module;

#include <OgreMatrix4.h>
#include <OgreVector4.h>
#include <OgreQuaternion.h>

export module Utils.HandSkeletonData;
export import Utils.EHandSkeletonBone;

export struct HandSkeletonData
{
    Ogre::Matrix4 handTransform;
    struct
    {
        Ogre::Vector4 bonePos;
        Ogre::Quaternion boneRot;
    } boneTransforms[(int)EHandSkeletonBone::Count];
};