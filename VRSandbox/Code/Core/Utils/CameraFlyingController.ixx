module;

#include <OgrePrerequisites.h>

export module Utils.CameraFlyingController;

export struct SDL_KeyboardEvent;
export struct SDL_MouseMotionEvent;
export enum class RenderMode;
export class World;

export class CameraFlyingController
{
public:

    CameraFlyingController(World& world, Ogre::Window* pWindow, Ogre::Node* pSceneNode, const Ogre::Camera* pCamera, RenderMode renderMode);
    virtual ~CameraFlyingController();
    CameraFlyingController(const CameraFlyingController& copy) = delete;

    void update(double timeSinceLast);

    /// Returns true if we've handled the event
    bool keyPressed(const SDL_KeyboardEvent& arg);
    /// Returns true if we've handled the event
    bool keyReleased(const SDL_KeyboardEvent& arg);

    void mouseMoved(const SDL_MouseMotionEvent& arg);

private:

    World& m_world;
    RenderMode m_renderMode;
    Ogre::Window* m_pWindow = nullptr;
    Ogre::Node* m_pNode = nullptr;
    const Ogre::Camera* m_pCamera = nullptr;
    bool  m_useSceneNode;
    bool  m_speedModifier;
    bool  m_wasd[4];
    bool  m_upDown[2];
    float m_yaw;
    float m_pitch;
    float m_camSpeed;
    float m_boostSpeed;
};

