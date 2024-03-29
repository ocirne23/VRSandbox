module;

#include <memory.h>

#include <OgrePrerequisites.h>
#include <OgreIdString.h>
#include <OgreVector3.h>
#include <OgreQuaternion.h>
#include <SDL_events.h>
#include <entt/fwd.hpp>

#include <openvr.h>

export module Systems.GraphicsSystem;

export struct SDL_Window;
export namespace Ogre { class HlmsUnlit; class HlmsPbs; namespace v1 { class OverlaySystem; } class Item; }
export class OpenVRCompositorListener;
export class NullCompositorListener;
export class DebugDrawer;
export struct GraphicsComponent;
export class World;

export enum class RenderMode
{
    VR,
    Desktop
};

export class GraphicsSystem
{
public:

    GraphicsSystem(World& world, entt::registry& registry);
    virtual ~GraphicsSystem();
    GraphicsSystem(const GraphicsSystem& copy) = delete;

    void initialize(const char* pWindowTitle, RenderMode renderMode);

    void update(double deltaSec);

    GraphicsComponent& addGraphicsComponent(entt::entity entity, Ogre::String meshName, Ogre::IdString datablockName = Ogre::IdString(""));
    void removeGraphicsComponent(entt::entity entity);

    void handleWindowEvent(SDL_Event& evt);
    void setWindowTitle(std::string str);
    bool isWindowVisible() const;
    bool isWindowFocused() const;

    Ogre::SceneManager* getSceneManager() const { return m_pSceneManager; }
    Ogre::Root* getRoot() const { return m_pRoot.get(); }
    Ogre::Window* getRenderWindow() const { return m_pRenderWindow; }
    SDL_Window* getSDLWindow() const { return m_pSDLWindow; }
    Ogre::Camera* getCamera() const { return m_pCamera; }
    OpenVRCompositorListener* getVRCompositor() const { return m_pOvrCompositorListener.get(); }
    vr::IVRSystem* getHMD() const { return m_pHMD; }
    Ogre::SceneNode* getCameraNode() const { return m_pCameraNode; }
    RenderMode getRenderMode() const { return m_renderMode; }
    DebugDrawer* getDebugDrawer() const { return m_pDebugDrawer.get(); }

private:

    void initializeWindow(const char* pWindowTitle);

private:

    World& m_world;
    entt::registry& m_registry;

    RenderMode m_renderMode;

    std::unique_ptr<Ogre::Root> m_pRoot;
    SDL_Window* m_pSDLWindow = nullptr;
    Ogre::Window* m_pRenderWindow = nullptr;
    Ogre::SceneManager* m_pSceneManager = nullptr;
    Ogre::Camera* m_pCamera = nullptr;
    Ogre::Camera* m_pCullCamera = nullptr;
    Ogre::SceneNode* m_pCameraNode = nullptr;
    Ogre::CompositorWorkspace* m_pVRMirrorWorkspace = nullptr;
    Ogre::CompositorWorkspace* m_pWorkspace = nullptr;
    Ogre::TextureGpu* m_pWorkspaceTexture = nullptr;
    Ogre::Item* m_hiddenAreaMeshVr = nullptr;

    vr::IVRSystem* m_pHMD = nullptr;
    std::string m_strDriver;
    std::string m_strDisplay;
    std::string m_deviceModelNumber;
    vr::TrackedDevicePose_t m_trackedDevicePose[vr::k_unMaxTrackedDeviceCount] = {};

    std::unique_ptr<OpenVRCompositorListener> m_pOvrCompositorListener;
    std::unique_ptr<Ogre::v1::OverlaySystem> m_pOverlaySystem;
    std::unique_ptr<Ogre::HlmsUnlit> m_pHlmsUnlit;
    std::unique_ptr<Ogre::HlmsPbs> m_pHlmsPbs;
    std::unique_ptr<DebugDrawer> m_pDebugDrawer;

    bool m_useMicrocodeCache = true;
    bool m_useHlmsDiskCache = true;

    double m_timeAccumulator = 0.0;
    int m_fpsCounter = 0;
};