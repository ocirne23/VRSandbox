#pragma once

#include <OgreMatrix4.h>
#include <openvr.h>

namespace VRMathUtils
{
	Ogre::Matrix4 convertSteamVRMatrixToMatrix4(vr::HmdMatrix34_t matPose)
	{
		Ogre::Matrix4 matrixObj(
			matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
			matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3],
			matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], matPose.m[2][3],
			0.0f, 0.0f, 0.0f, 1.0f);
		return matrixObj;
	}
}
