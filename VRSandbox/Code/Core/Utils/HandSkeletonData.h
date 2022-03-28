#pragma once

#include "EHandSkeletonBone.h"

#include <OgreMatrix4.h>
#include <OgreVector4.h>
#include <OgreQuaternion.h>

struct HandSkeletonData
{
    Ogre::Matrix4 handTransform;
    struct
    {
        Ogre::Vector4 bonePos;
        Ogre::Quaternion boneRot;
    } boneTransforms[EHandSkeletonBone::eBone_Count];
};