#pragma once

#include <OgrePrerequisites.h>

struct SDL_KeyboardEvent;
struct SDL_MouseMotionEvent;

enum class RenderMode;

class CameraController
{
public:

    CameraController(Ogre::Window* pWindow, Ogre::Node* pSceneNode, const Ogre::Camera* pCamera, RenderMode renderMode);
    virtual ~CameraController();
    CameraController(const CameraController& copy) = delete;

    void update(double timeSinceLast);

    /// Returns true if we've handled the event
    bool keyPressed(const SDL_KeyboardEvent& arg);
    /// Returns true if we've handled the event
    bool keyReleased(const SDL_KeyboardEvent& arg);

    void mouseMoved(const SDL_MouseMotionEvent& arg);

private:

    RenderMode m_renderMode;
    Ogre::Window* m_pWindow = nullptr;
    Ogre::Node* m_pNode = nullptr;
    const Ogre::Camera* m_pCamera = nullptr;
    bool  mUseSceneNode;
    bool  mSpeedMofifier;
    bool  mWASD[4];
    bool  mSlideUpDown[2];
    float mCameraYaw;
    float mCameraPitch;
    float mCameraBaseSpeed;
    float mCameraSpeedBoost;
};

