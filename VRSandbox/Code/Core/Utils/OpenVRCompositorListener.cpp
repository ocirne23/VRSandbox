module;

#include <OgreTextureGpu.h>
#include <OgreRenderSystem.h>
#include <Compositor/Pass/OgreCompositorPass.h>
#include <Compositor/Pass/OgreCompositorPassDef.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassScene.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreLogManager.h>
#include <filesystem>
#include <openvr.h>

module Utils.OpenVRCompositorListener;

import Utils.DebugDrawer;

OpenVRCompositorListener::OpenVRCompositorListener(
    vr::IVRSystem* hmd, vr::IVRCompositor* vrCompositor,
    Ogre::TextureGpu* vrTexture, Ogre::Root* root,
    Ogre::CompositorWorkspace* workspace,
    Ogre::Camera* camera, Ogre::Camera* cullCamera,
    Ogre::SceneNode* pControllerNode) :
    m_pIVRSystem(hmd),
    m_pVrCompositor(vrCompositor),
    m_pVrTexture(vrTexture),
    m_pRoot(root),
    m_renderSystem(root->getRenderSystem()),
    m_workspace(workspace),
    m_validPoseCount(0),
    m_apiTextureType(vr::TextureType_Invalid),
    m_pCamera(camera),
    m_pVrCullCamera(cullCamera),
    m_cullCameraOffset(Ogre::Vector3::ZERO),
    m_waitingMode(VrWaitingMode::BeforeSceneGraph),
    m_firstGlitchFreeMode(VrWaitingMode::NumVrWaitingModes),
    m_lastCamNear(0),
    m_lastCamFar(0),
    m_mustSyncAtEndOfFrame(false),
    m_pControllerNode(pControllerNode)
{
    memset(m_trackedDevicePose, 0, sizeof(m_trackedDevicePose));
    memset(m_devicePose, 0, sizeof(m_devicePose));
    memset(&m_vrData, 0, sizeof(m_vrData));

    m_pCamera->setVrData(&m_vrData);
    syncCameraProjection(true);

    m_pRoot->addFrameListener(this);
    m_workspace->addListener(this);

    const Ogre::String& renderSystemName = m_renderSystem->getName();
    if (renderSystemName == "OpenGL 3+ Rendering Subsystem")
        m_apiTextureType = vr::TextureType_OpenGL;
    else if (renderSystemName == "Direct3D11 Rendering Subsystem")
        m_apiTextureType = vr::TextureType_DirectX;
    else if (renderSystemName == "Metal Rendering Subsystem")
        m_apiTextureType = vr::TextureType_Metal;
}
//-------------------------------------------------------------------------
OpenVRCompositorListener::~OpenVRCompositorListener()
{
    if (m_pCamera)
        m_pCamera->setVrData(0);

    m_workspace->removeListener(this);
    m_pRoot->removeFrameListener(this);
}
//-------------------------------------------------------------------------
Ogre::Matrix4 OpenVRCompositorListener::convertSteamVRMatrixToMatrix4(vr::HmdMatrix34_t matPose)
{
    Ogre::Matrix4 matrixObj(
        matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
        matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3],
        matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], matPose.m[2][3],
        0.0f, 0.0f, 0.0f, 1.0f);
    return matrixObj;
}
//-------------------------------------------------------------------------
Ogre::Matrix4 OpenVRCompositorListener::convertSteamVRMatrixToMatrix4(vr::HmdMatrix44_t matPose)
{
    Ogre::Matrix4 matrixObj(
        matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
        matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3],
        matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], matPose.m[2][3],
        matPose.m[3][0], matPose.m[3][1], matPose.m[3][2], matPose.m[3][3]);
    return matrixObj;
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::updateHmdTrackingPose(void)
{
    m_pVrCompositor->WaitGetPoses(m_trackedDevicePose, vr::k_unMaxTrackedDeviceCount, NULL, 0);

    m_validPoseCount = 0;
    for (size_t nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice)
    {
        if (m_trackedDevicePose[nDevice].bPoseIsValid)
        {
            ++m_validPoseCount;
            m_devicePose[nDevice] = convertSteamVRMatrixToMatrix4(
                m_trackedDevicePose[nDevice].mDeviceToAbsoluteTracking);
        }
    }

    if (m_trackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
    {
        const bool canSync = canSyncCameraTransformImmediately();
        if (canSync)
            syncCamera();
        else
            m_mustSyncAtEndOfFrame = true;
    }
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::syncCullCamera(void)
{
    const Ogre::Quaternion derivedRot = m_pCamera->getDerivedOrientation();
    Ogre::Vector3 camPos = m_pCamera->getDerivedPosition();
    m_pVrCullCamera->setOrientation(derivedRot);
    m_pVrCullCamera->setPosition(camPos + derivedRot * m_cullCameraOffset);
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::syncCamera(void)
{
    OGRE_ASSERT_MEDIUM(m_trackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid);
    m_pCamera->setPosition(m_devicePose[vr::k_unTrackedDeviceIndex_Hmd].getTrans() + m_pControllerNode->getPosition());
    m_pCamera->setOrientation(Ogre::Quaternion(m_pControllerNode->getOrientation().getYaw(), Ogre::Vector3(0, 1, 0)) * m_devicePose[vr::k_unTrackedDeviceIndex_Hmd].extractQuaternion());
    /*
    Ogre::Matrix3 m;
    m_pCamera->getOrientation().ToRotationMatrix(m);
    Ogre::Matrix3 rotation;
    rotation.FromAngleAxis(Ogre::Vector3(0, 1, 0), m_pControllerNode->getOrientation().getYaw());
    m_pCamera->setOrientation(Ogre::Quaternion(m * rotation));*/
    if (m_waitingMode < VrWaitingMode::AfterFrustumCulling)
        syncCullCamera();

    m_mustSyncAtEndOfFrame = false;
}

void OpenVRCompositorListener::syncControllers()
{

}

//-------------------------------------------------------------------------
void OpenVRCompositorListener::syncCameraProjection(bool bForceUpdate)
{
    const Ogre::Real camNear = m_pCamera->getNearClipDistance();
    const Ogre::Real camFar = m_pCamera->getFarClipDistance();

    if (m_lastCamNear != camNear || m_lastCamFar != camFar || bForceUpdate)
    {
        Ogre::Matrix4 eyeToHead[2];
        Ogre::Matrix4 projectionMatrix[2];
        Ogre::Matrix4 projectionMatrixRS[2];
        Ogre::Vector4 eyeFrustumExtents[2];

        for (size_t i = 0u; i < 2u; ++i)
        {
            vr::EVREye eyeIdx = static_cast<vr::EVREye>(i);
            eyeToHead[i] = convertSteamVRMatrixToMatrix4(m_pIVRSystem->GetEyeToHeadTransform(eyeIdx));
            projectionMatrix[i] = convertSteamVRMatrixToMatrix4(m_pIVRSystem->GetProjectionMatrix(eyeIdx, camNear, camFar));
            m_renderSystem->_convertOpenVrProjectionMatrix(projectionMatrix[i], projectionMatrixRS[i]);
            m_pIVRSystem->GetProjectionRaw(eyeIdx, &eyeFrustumExtents[i].x, &eyeFrustumExtents[i].y, &eyeFrustumExtents[i].z, &eyeFrustumExtents[i].w);
        }

        m_vrData.set(eyeToHead, projectionMatrixRS);
        m_lastCamNear = camNear;
        m_lastCamFar = camFar;

        Ogre::Vector4 cameraCullFrustumExtents;
        cameraCullFrustumExtents.x = std::min(eyeFrustumExtents[0].x, eyeFrustumExtents[1].x);
        cameraCullFrustumExtents.y = std::max(eyeFrustumExtents[0].y, eyeFrustumExtents[1].y);
        cameraCullFrustumExtents.z = std::max(eyeFrustumExtents[0].z, eyeFrustumExtents[1].z);
        cameraCullFrustumExtents.w = std::min(eyeFrustumExtents[0].w, eyeFrustumExtents[1].w);

        m_pVrCullCamera->setFrustumExtents(cameraCullFrustumExtents.x, cameraCullFrustumExtents.y,
            cameraCullFrustumExtents.w, cameraCullFrustumExtents.z,  Ogre::FET_TAN_HALF_ANGLES);

        const float ipd = m_vrData.mLeftToRight.x;
        m_cullCameraOffset = Ogre::Vector3::ZERO;
        m_cullCameraOffset.z = (ipd / 2.0f) / Ogre::Math::Abs(cameraCullFrustumExtents.x);

        const Ogre::Real offset = m_cullCameraOffset.length();
        m_pVrCullCamera->setNearClipDistance(camNear + offset);
        m_pVrCullCamera->setFarClipDistance(camFar + offset);
    }
}
//-------------------------------------------------------------------------
bool OpenVRCompositorListener::frameStarted(const Ogre::FrameEvent& evt)
{
    if (m_waitingMode == VrWaitingMode::BeforeSceneGraph)
        updateHmdTrackingPose();
    return true;
}
//-------------------------------------------------------------------------
bool OpenVRCompositorListener::frameRenderingQueued(const Ogre::FrameEvent& evt)
{
    vr::VRTextureBounds_t texBounds;
    if (m_pVrTexture->requiresTextureFlipping())
    {
        texBounds.vMin = 1.0f;
        texBounds.vMax = 0.0f;
    }
    else
    {
        texBounds.vMin = 0.0f;
        texBounds.vMax = 1.0f;
    }

    vr::Texture_t eyeTexture =
    {
        0,
        m_apiTextureType,
        vr::ColorSpace_Gamma
    };
    m_pVrTexture->getCustomAttribute(Ogre::TextureGpu::msFinalTextureBuffer, &eyeTexture.handle);

    texBounds.uMin = 0;
    texBounds.uMax = 0.5f;
    m_pVrCompositor->Submit(vr::Eye_Left, &eyeTexture, &texBounds);
    texBounds.uMin = 0.5f;
    texBounds.uMax = 1.0f;
    m_pVrCompositor->Submit(vr::Eye_Right, &eyeTexture, &texBounds);

    m_renderSystem->flushCommands();


    vr::VREvent_t event;
    while (m_pIVRSystem->PollNextEvent(&event, sizeof(event)))
    {
        if (event.trackedDeviceIndex != vr::k_unTrackedDeviceIndex_Hmd &&
            event.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid)
        {
            continue;
        }

        switch (event.eventType)
        {
        case vr::VREvent_TrackedDeviceUpdated:
        case vr::VREvent_IpdChanged:
        case vr::VREvent_ChaperoneDataHasChanged:
            syncCameraProjection(true);
            break;
        }
    }

    return true;
}
//-------------------------------------------------------------------------
bool OpenVRCompositorListener::frameEnded(const Ogre::FrameEvent& evt)
{
    syncCameraProjection(false);
    if (m_waitingMode == VrWaitingMode::AfterSwap)
        updateHmdTrackingPose();
    else
        m_pVrCompositor->PostPresentHandoff();
    if (m_mustSyncAtEndOfFrame)
        syncCamera();
    if (m_waitingMode >= VrWaitingMode::AfterFrustumCulling)
        syncCullCamera();
    /*
    {
        auto leftIdx = m_iVRSystem->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
        auto rightIdx = m_iVRSystem->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

        if (leftIdx != vr::k_unTrackedDeviceIndexInvalid)
        {
            auto& leftPose = mTrackedDevicePose[leftIdx];
            if (leftPose.bPoseIsValid && leftPose.bDeviceIsConnected)
            {
                m_leftControllerPosition = convertSteamVRMatrixToMatrix4(leftPose.mDeviceToAbsoluteTracking);
            }
        }
        if (rightIdx != vr::k_unTrackedDeviceIndexInvalid)
        {
            auto& rightPose = mTrackedDevicePose[rightIdx];
            if (rightPose.bPoseIsValid && rightPose.bDeviceIsConnected)
            {
                m_rightControllerPosition = convertSteamVRMatrixToMatrix4(rightPose.mDeviceToAbsoluteTracking);
            }
        }
    }*/
    return true;
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::workspacePreUpdate(Ogre::CompositorWorkspace* workspace)
{
    if (m_waitingMode == VrWaitingMode::AfterSceneGraph)
        updateHmdTrackingPose();
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::passPreExecute(Ogre::CompositorPass* pass)
{
    if (m_waitingMode == VrWaitingMode::BeforeShadowmaps &&
        pass->getDefinition()->getType() == Ogre::PASS_SCENE &&
        pass->getDefinition()->mIdentifier == 0x01234567)
    {
        updateHmdTrackingPose();
    }
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::passSceneAfterShadowMaps(Ogre::CompositorPassScene* pass)
{
    if (m_waitingMode == VrWaitingMode::BeforeFrustumCulling &&
        pass->getDefinition()->mIdentifier == 0x01234567)
    {
        updateHmdTrackingPose();
    }
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::passSceneAfterFrustumCulling(Ogre::CompositorPassScene* pass)
{
    if (m_waitingMode == VrWaitingMode::AfterFrustumCulling &&
        pass->getDefinition()->mIdentifier == 0x01234567)
    {
        updateHmdTrackingPose();
    }
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::setWaitingMode(VrWaitingMode::VrWaitingMode waitingMode)
{
    m_waitingMode = waitingMode;
}
//-------------------------------------------------------------------------
void OpenVRCompositorListener::setGlitchFree(VrWaitingMode::VrWaitingMode firstGlitchFreeMode)
{
    m_firstGlitchFreeMode = firstGlitchFreeMode;
}
//-------------------------------------------------------------------------
bool OpenVRCompositorListener::canSyncCameraTransformImmediately(void) const
{
    return m_waitingMode <= VrWaitingMode::BeforeSceneGraph ||
        m_waitingMode <= m_firstGlitchFreeMode;
}

