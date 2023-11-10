module;

#include <entt/entity/registry.hpp>

#include <SDL.h>
#include <SDL_syswm.h>

#include <OgreRoot.h>
#include <OgreException.h>
#include <OgreLogManager.h>
#include <OgreConfigFile.h>
#include <OgreItem.h>
#include <OgreMeshManager.h>
#include <OgreMeshManager2.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsPbs.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsDiskCache.h>
#include <OgreArchiveManager.h>
#include <OgreTextureGpuManager.h>
#include <OgreGpuProgramManager.h>
#include <OgreOverlaySystem.h>
#include <OgreOverlayManager.h>
#include <OgreWindow.h>
#include <OgreCamera.h>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorChannel.h>
#include <OgreHiddenAreaMeshVr.h>
#include <OgrePlatformInformation.h>
#include <OgreHlmsPbsDatablock.h>

#include <openvr.h>
#include <fstream>
#include <filesystem>

module Systems.GraphicsSystem;

import Components.SceneComponent;
import Components.GraphicsComponent;

import Utils.DebugDrawer;
import Utils.NullCompositorListener;
import Utils.OpenVRCompositorListener;

namespace
{
    std::string GetTrackedDeviceString(vr::TrackedDeviceIndex_t unDevice,
        vr::TrackedDeviceProperty prop,
        vr::TrackedPropertyError* peError = nullptr)
    {
        vr::IVRSystem* vrSystem = vr::VRSystem();
        uint32_t unRequiredBufferLen = vrSystem->GetStringTrackedDeviceProperty(unDevice, prop, NULL, 0, peError);
        if (unRequiredBufferLen == 0)
            return "";
        std::unique_ptr<char[]> pchBuffer(new char[unRequiredBufferLen]);
        unRequiredBufferLen = vrSystem->GetStringTrackedDeviceProperty(unDevice, prop, pchBuffer.get(), unRequiredBufferLen, peError);
        std::string sResult = pchBuffer.get();
        pchBuffer.release();
        return sResult;
    }
}

GraphicsSystem::GraphicsSystem(World& world, entt::registry& registry) : m_world(world), m_registry(registry)
{

}

void GraphicsSystem::initializeWindow(const char* pWindowTitle)
{
    const int width = 1500;
    const int height = 1000;
    const int screen = 0;
    const int posX = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);
    const int posY = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);
    const bool fullscreen = false;
    m_pSDLWindow = SDL_CreateWindow(pWindowTitle, posX, posY, width, height,
        SDL_WINDOW_SHOWN | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0) | SDL_WINDOW_RESIZABLE);

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(m_pSDLWindow, &wmInfo) == SDL_FALSE)
    {
        OGRE_EXCEPT(Ogre::Exception::ERR_INTERNAL_ERROR,
            "Couldn't get WM Info! (SDL2)",
            "GraphicsSystem::initialize");
    }
    Ogre::ConfigOptionMap& cfgOpts = m_pRoot->getRenderSystem()->getConfigOptions();

    Ogre::NameValuePairList params;
    params.insert(std::make_pair("externalWindowHandle", Ogre::StringConverter::toString((uintptr_t)wmInfo.info.win.window)));
    params.insert(std::make_pair("title", pWindowTitle));
    params.insert(std::make_pair("gamma", cfgOpts["sRGB Gamma Conversion"].currentValue));
    if (cfgOpts.find("VSync Method") != cfgOpts.end())
        params.insert(std::make_pair("vsync_method", cfgOpts["VSync Method"].currentValue));
    params.insert(std::make_pair("FSAA", cfgOpts["FSAA"].currentValue));
    params.insert(std::make_pair("vsync", cfgOpts["VSync"].currentValue));
    params.insert(std::make_pair("reverse_depth", "Yes"));

    m_pRenderWindow = Ogre::Root::getSingleton().createRenderWindow(pWindowTitle, width, height, fullscreen, &params);
}

void GraphicsSystem::initialize(const char* pWindowTitle, RenderMode renderMode)
{
    m_renderMode = renderMode;
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
    {
        OGRE_EXCEPT(Ogre::Exception::ERR_INTERNAL_ERROR, "Cannot initialize SDL2!", "Graphics::initialize");
    }

#if OGRE_DEBUG_MODE == OGRE_DEBUG_LEVEL_DEBUG
    const char* pluginsFile = "plugins_d.cfg";
#else
    const char* pluginsFile = "plugins.cfg";
#endif
    m_pRoot = std::make_unique<Ogre::Root>(nullptr, pluginsFile, "ogre.cfg", "Ogre.log");
    m_pRoot->setRenderSystem(m_pRoot->getRenderSystemByName("Direct3D11 Rendering Subsystem"));
    m_pRoot->getRenderSystem()->setConfigOption("sRGB Gamma Conversion", "Yes");
    m_pRoot->initialise(false);
    initializeWindow(pWindowTitle);

    m_pOverlaySystem = std::make_unique<Ogre::v1::OverlaySystem>();

    Ogre::ConfigFile cf;
    cf.loadDirect("resources.cfg");
    Ogre::String assetsFolderPath = cf.getSetting("AssetsFolderLocation", "General", "");
    if (assetsFolderPath.empty())
        assetsFolderPath = "./";
    else if (*(assetsFolderPath.end() - 1) != '/')
        assetsFolderPath += "/";

    std::vector<std::string> subdirectories;
    for (auto& dir : std::filesystem::recursive_directory_iterator(std::string(assetsFolderPath.c_str())))
        if (dir.is_directory())
        {
            std::string path = dir.path().string();
            std::replace(path.begin(), path.end(), '\\', '/');
            subdirectories.push_back(path);
            Ogre::ResourceGroupManager::getSingleton().addResourceLocation(path, "FileSystem", "General");
        }

    Ogre::ArchiveManager& archiveManager = Ogre::ArchiveManager::getSingleton();
    const Ogre::String& archiveType = "FileSystem";

    Ogre::String mainFolderPath;
    Ogre::StringVector libraryFoldersPaths;
    {
        Ogre::HlmsUnlit::getDefaultPaths(mainFolderPath, libraryFoldersPaths);
        Ogre::ArchiveVec archiveLibraryFolders;
        for (auto it : libraryFoldersPaths)
            archiveLibraryFolders.push_back(archiveManager.load(assetsFolderPath + it, archiveType, true));
        m_pHlmsUnlit.reset(new Ogre::HlmsUnlit(archiveManager.load(assetsFolderPath + mainFolderPath, archiveType, true), &archiveLibraryFolders));
        m_pHlmsUnlit->setDebugOutputPath(false, false);
        Ogre::Root::getSingleton().getHlmsManager()->registerHlms(m_pHlmsUnlit.get());
    }
    {
        Ogre::HlmsPbs::getDefaultPaths(mainFolderPath, libraryFoldersPaths);
        Ogre::ArchiveVec archiveLibraryFolders;
        for (auto it : libraryFoldersPaths)
            archiveLibraryFolders.push_back(archiveManager.load(assetsFolderPath + it, archiveType, true));
        m_pHlmsPbs.reset(new Ogre::HlmsPbs(archiveManager.load(assetsFolderPath + mainFolderPath, archiveType, true), &archiveLibraryFolders));
        m_pHlmsPbs->setDebugOutputPath(false, false);
        Ogre::Root::getSingleton().getHlmsManager()->registerHlms(m_pHlmsPbs.get());
    }
    Ogre::RenderSystem* renderSystem = m_pRoot->getRenderSystem();
    if (renderSystem->getName() == "Direct3D11 Rendering Subsystem")
    {
        //Set lower limits 512kb instead of the default 4MB per Hlms in D3D 11.0
        //and below to avoid saturating AMD's discard limit (8MB) or
        //saturate the PCIE bus in some low end machines.
        bool supportsNoOverwriteOnTextureBuffers;
        renderSystem->getCustomAttribute("MapNoOverwriteOnDynamicBufferSRV", &supportsNoOverwriteOnTextureBuffers);
        if (!supportsNoOverwriteOnTextureBuffers)
        {
            m_pHlmsPbs->setTextureBufferDefaultSize(512 * 1024);
            m_pHlmsUnlit->setTextureBufferDefaultSize(512 * 1024);
        }
    }

    // TODO: ensure write access
    Ogre::Archive* rwAccessFolderArchive = archiveManager.load("", "FileSystem", true);
    try
    {
        const Ogre::String filename = "gen/textureMetadataCache.json";
        if (rwAccessFolderArchive->exists(filename))
        {
            Ogre::DataStreamPtr stream = rwAccessFolderArchive->open(filename);
            std::vector<char> fileData;
            fileData.resize(stream->size() + 1);
            if (!fileData.empty())
            {
                stream->read(&fileData[0], stream->size());
                fileData.back() = '\0';
                m_pRoot->getRenderSystem()->getTextureGpuManager()->importTextureMetadataCache(stream->getName(), &fileData[0], false);
            }
        }
        else
        {
            Ogre::LogManager::getSingleton().logMessage("[INFO] Texture cache not found at /textureMetadataCache.json");
        }
    }
    catch (Ogre::Exception& e)
    {
        Ogre::LogManager::getSingleton().logMessage(e.getFullDescription());
    }

    Ogre::HlmsManager* hlmsManager = m_pRoot->getHlmsManager();
    Ogre::HlmsDiskCache diskCache(hlmsManager);

    if (m_useMicrocodeCache)
    {
        //Make sure the microcode cache is enabled.
        Ogre::GpuProgramManager::getSingleton().setSaveMicrocodesToCache(true);
        const Ogre::String filename = "gen/microcodeCodeCache.cache";
        if (rwAccessFolderArchive->exists(filename))
        {
            Ogre::DataStreamPtr shaderCacheFile = rwAccessFolderArchive->open(filename);
            Ogre::GpuProgramManager::getSingleton().loadMicrocodeCache(shaderCacheFile);
        }
    }
    if (m_useHlmsDiskCache)
    {
        for (size_t i = Ogre::HLMS_LOW_LEVEL + 1u; i < Ogre::HLMS_MAX; ++i)
        {
            Ogre::Hlms* hlms = hlmsManager->getHlms(static_cast<Ogre::HlmsTypes>(i));
            if (hlms)
            {
                Ogre::String filename = "gen/hlmsDiskCache" + Ogre::StringConverter::toString(i) + ".bin";
                try
                {
                    if (rwAccessFolderArchive->exists(filename))
                    {
                        Ogre::DataStreamPtr diskCacheFile = rwAccessFolderArchive->open(filename);
                        diskCache.loadFrom(diskCacheFile);
                        diskCache.applyTo(hlms);
                    }
                }
                catch (Ogre::Exception&)
                {
                    Ogre::LogManager::getSingleton().logMessage("Error loading cache from " + filename + "! If you have issues, try deleting the file "
                        "and restarting the app");
                }
            }
        }
    }
    archiveManager.unload(rwAccessFolderArchive);

    // Initialise, parse scripts etc
    Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups(true);

    // Initialize resources for LTC area lights and accurate specular reflections (IBL)
    Ogre::Hlms* hlms = m_pRoot->getHlmsManager()->getHlms(Ogre::HLMS_PBS);
    OGRE_ASSERT_HIGH(dynamic_cast<Ogre::HlmsPbs*>(hlms));
    Ogre::HlmsPbs* hlmsPbs = static_cast<Ogre::HlmsPbs*>(hlms);
    try
    {
        hlmsPbs->loadLtcMatrix();
    }
    catch (Ogre::FileNotFoundException& e)
    {
        Ogre::LogManager::getSingleton().logMessage(e.getFullDescription(), Ogre::LML_CRITICAL);
        Ogre::LogManager::getSingleton().logMessage(
            "WARNING: LTC matrix textures could not be loaded. Accurate specular IBL reflections "
            "and LTC area lights won't be available or may not function properly!",
            Ogre::LML_CRITICAL);
    }

#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
    //Debugging multithreaded code is a PITA, disable it.
    const size_t numThreads = 1;
#else
    //getNumLogicalCores() may return 0 if couldn't detect
    const size_t numThreads = std::max<size_t>(1, Ogre::PlatformInformation::getNumLogicalCores());
#endif
    // Create the SceneManager, in this case a generic one
    m_pSceneManager = m_pRoot->createSceneManager(Ogre::ST_GENERIC, numThreads, "ExampleSMInstance");

    Ogre::HlmsPbsDatablock* datablock = static_cast<Ogre::HlmsPbsDatablock*>(hlmsPbs->createDatablock("debug_draw", "debug_draw", Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
    datablock->setDiffuse(Ogre::Vector3(0.0f, 1.0f, 0.0f));
    datablock->setRoughness(1.0f);
    m_pDebugDrawer = std::make_unique<DebugDrawer>(m_pSceneManager, 0.0f);

    m_pSceneManager->addRenderQueueListener(m_pOverlaySystem.get());
    m_pSceneManager->getRenderQueue()->setSortRenderQueue(Ogre::v1::OverlayManager::getSingleton().mDefaultRenderQueueId, Ogre::RenderQueue::StableSort);
    m_pSceneManager->setShadowDirectionalLightExtrusionDistance(500.0f);
    m_pSceneManager->setShadowFarDistance(500.0f);

    m_pCamera = m_pSceneManager->createCamera("Main Camera");
    m_pCamera->setNearClipDistance(0.05f);
    m_pCamera->setFarClipDistance(1000.0f);
    m_pCamera->setAutoAspectRatio(true);

    m_pCameraNode = m_pSceneManager->createSceneNode(Ogre::SCENE_DYNAMIC);

    Ogre::CompositorManager2* compositorManager = m_pRoot->getCompositorManager2();

    if (renderMode == RenderMode::VR)
    {
        bool c_useRDM = renderMode == RenderMode::VR;
        const Ogre::IdString workspaceName = c_useRDM ? "Tutorial_OpenVRWorkspaceRDM" : "Tutorial_OpenVRWorkspaceNoRDM";

        // Loading the SteamVR Runtime
        vr::EVRInitError eError = vr::VRInitError_None;
        m_pHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);

        if (eError != vr::VRInitError_None)
        {
            m_pHMD = 0;
            Ogre::String errorMsg = "Unable to init VR runtime: ";
            errorMsg += vr::VR_GetVRInitErrorAsEnglishDescription(eError);
            OGRE_EXCEPT(Ogre::Exception::ERR_RENDERINGAPI_ERROR, errorMsg, "Tutorial_OpenVRGraphicsSystem::initOpenVR");
        }

        m_strDriver = "No Driver";
        m_strDisplay = "No Display";
        m_strDriver = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
        m_strDisplay = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);
        m_deviceModelNumber = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String);

        if (!vr::VRCompositor())
        {
            OGRE_EXCEPT(Ogre::Exception::ERR_RENDERINGAPI_ERROR,
                "VR Compositor initialization failed. See log file for details",
                "Tutorial_OpenVRGraphicsSystem::initCompositorVR");
        }

        uint32_t width, height;
        m_pHMD->GetRecommendedRenderTargetSize(&width, &height);

        Ogre::TextureGpuManager* textureManager = m_pRoot->getRenderSystem()->getTextureGpuManager();
        //Radial Density Mask requires the VR texture to be UAV & reinterpretable
        m_pWorkspaceTexture = textureManager->createOrRetrieveTexture("OpenVR Both Eyes", Ogre::GpuPageOutStrategy::Discard,
            Ogre::TextureFlags::RenderToTexture | Ogre::TextureFlags::Uav | Ogre::TextureFlags::Reinterpretable, Ogre::TextureTypes::Type2D);
        m_pWorkspaceTexture->setResolution(width << 1u, height);
        m_pWorkspaceTexture->setPixelFormat(Ogre::PFG_RGBA8_UNORM_SRGB);
        if (!c_useRDM)
            m_pWorkspaceTexture->setSampleDescription(Ogre::SampleDescription(4u));
        m_pWorkspaceTexture->scheduleTransitionTo(Ogre::GpuResidency::Resident);

        m_pCullCamera = m_pSceneManager->createCamera("CullCamera");
        m_pWorkspace = compositorManager->addWorkspace(m_pSceneManager, m_pWorkspaceTexture, m_pCamera, workspaceName, true, 0);
        m_pOvrCompositorListener = std::make_unique<OpenVRCompositorListener>(m_pHMD, vr::VRCompositor(), m_pWorkspaceTexture,
            m_pRoot.get(), m_pWorkspace, m_pCamera, m_pCullCamera, m_pCameraNode);

        if (c_useRDM)
        {
            const float radiuses[3] = { 0.25f, 0.7f, 0.85f };
            m_pSceneManager->setRadialDensityMask(true, radiuses);
        }

        try
        {
            Ogre::ConfigFile cfgFile;
            cfgFile.load("HiddenAreaMeshVr.cfg");
            Ogre::HiddenAreaVrSettings setting = Ogre::HiddenAreaMeshVrGenerator::loadSettings(m_deviceModelNumber, cfgFile);
            if (setting.tessellation > 0u)
                Ogre::HiddenAreaMeshVrGenerator::generate("HiddenAreaMeshVr.mesh", setting);
        }
        catch (Ogre::FileNotFoundException& e)
        {
            Ogre::LogManager& logManager = Ogre::LogManager::getSingleton();
            logManager.logMessage(e.getDescription());
            logManager.logMessage("HiddenAreaMeshVR optimization won't be available");
        }

        const bool bIsHamVrOptEnabled = !Ogre::MeshManager::getSingleton().getByName("HiddenAreaMeshVr.mesh", Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME).isNull();
        if (bIsHamVrOptEnabled)
        {
            m_hiddenAreaMeshVr = m_pSceneManager->createItem("HiddenAreaMeshVr.mesh", Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME, Ogre::SCENE_STATIC);
            m_hiddenAreaMeshVr->setCastShadows(false);
            m_hiddenAreaMeshVr->setRenderQueueGroup(0u);
            m_hiddenAreaMeshVr->getSubItem(0)->setUseIdentityProjection(true);
            // Set to render *after* the RadialDensityMask
            m_hiddenAreaMeshVr->getSubItem(0)->setRenderQueueSubGroup(1u);
            m_pSceneManager->getRootSceneNode(Ogre::SCENE_STATIC)->attachObject(m_hiddenAreaMeshVr);
        }

        Ogre::CompositorChannelVec channels(2u);
        channels[0] = m_pRenderWindow->getTexture();
        channels[1] = m_pWorkspaceTexture;
        m_pVRMirrorWorkspace = compositorManager->addWorkspace(m_pSceneManager, channels, m_pCamera, "Tutorial_OpenVRMirrorWindowWorkspace", true);
    }
    else
    {
        m_pWorkspace = compositorManager->addWorkspace(m_pSceneManager, m_pRenderWindow->getTexture(), m_pCamera, "PbsMaterialsWorkspace", true);
    }
}


GraphicsSystem::~GraphicsSystem()
{
    if (m_pRoot->getRenderSystem())
    {
        Ogre::TextureGpuManager* textureManager = m_pRoot->getRenderSystem()->getTextureGpuManager();
        if (textureManager)
        {
            Ogre::String jsonString;
            textureManager->exportTextureMetadataCache(jsonString);
            const Ogre::String path = "/textureMetadataCache.json";
            std::ofstream file(path.c_str(), std::ios::binary | std::ios::out);
            if (file.is_open())
                file.write(jsonString.c_str(), static_cast<std::streamsize>(jsonString.size()));
            file.close();
        }
    }

    if (m_pRoot->getRenderSystem() && Ogre::GpuProgramManager::getSingletonPtr() && (m_useMicrocodeCache || m_useHlmsDiskCache))
    {
        Ogre::HlmsManager* hlmsManager = m_pRoot->getHlmsManager();
        Ogre::HlmsDiskCache diskCache(hlmsManager);
        Ogre::ArchiveManager& archiveManager = Ogre::ArchiveManager::getSingleton();
        Ogre::Archive* rwAccessFolderArchive = archiveManager.load("", "FileSystem", false);

        if (m_useHlmsDiskCache)
        {
            for (size_t i = Ogre::HLMS_LOW_LEVEL + 1u; i < Ogre::HLMS_MAX; ++i)
            {
                Ogre::Hlms* hlms = hlmsManager->getHlms(static_cast<Ogre::HlmsTypes>(i));
                if (hlms)
                {
                    diskCache.copyFrom(hlms);
                    Ogre::DataStreamPtr diskCacheFile = rwAccessFolderArchive->create("hlmsDiskCache" + Ogre::StringConverter::toString(i) + ".bin");
                    diskCache.saveTo(diskCacheFile);
                }
            }
        }

        if (Ogre::GpuProgramManager::getSingleton().isCacheDirty() && m_useMicrocodeCache)
        {
            const Ogre::String filename = "microcodeCodeCache.cache";
            Ogre::DataStreamPtr shaderCacheFile = rwAccessFolderArchive->create(filename);
            Ogre::GpuProgramManager::getSingleton().saveMicrocodeCache(shaderCacheFile);
        }

        archiveManager.unload("");
    }

    Ogre::CompositorManager2* compositorManager = m_pRoot->getCompositorManager2();
    compositorManager->removeWorkspace(m_pWorkspace);
    if (m_pVRMirrorWorkspace)
        compositorManager->removeWorkspace(m_pVRMirrorWorkspace);

    m_pHlmsUnlit.release();
    m_pHlmsPbs.release();

    m_pOvrCompositorListener.release();

    if (m_pWorkspaceTexture)
    {
        Ogre::TextureGpuManager* textureManager = m_pRoot->getRenderSystem()->getTextureGpuManager();
        textureManager->destroyTexture(m_pWorkspaceTexture);
        m_pWorkspaceTexture = nullptr;
    }

    if (m_pCullCamera)
    {
        m_pSceneManager->destroyCamera(m_pCullCamera);
        m_pCullCamera = nullptr;
    }

    if (m_pHMD)
    {
        vr::VR_Shutdown();
        m_pHMD = nullptr;
    }

    m_pDebugDrawer.reset();
    m_pSceneManager->removeRenderQueueListener(m_pOverlaySystem.get());

    m_pOverlaySystem.release();
    m_pRoot.release();

    if (m_pSDLWindow)
    {
        SDL_SetWindowFullscreen(m_pSDLWindow, 0);
        SDL_DestroyWindow(m_pSDLWindow);
        m_pSDLWindow = nullptr;
    }
    SDL_Quit();
}

void GraphicsSystem::update(double deltaSec)
{
    m_pCamera->setPosition(m_pCameraNode->getPosition());
    m_pCamera->setOrientation(m_pCameraNode->getOrientation());

    m_pDebugDrawer->build();
    if (m_pRenderWindow->isVisible())
        m_pRoot->renderOneFrame();
    m_pDebugDrawer->clear();

    m_timeAccumulator += deltaSec;
    if (m_timeAccumulator >= 1.0)
    {
        setWindowTitle("fps: " + std::to_string(m_fpsCounter) + " ms: " + std::to_string((1.0 / m_fpsCounter) * 1000.0));
        m_timeAccumulator -= 1.0;
        m_fpsCounter = 0;
    }
    m_fpsCounter++;
}

GraphicsComponent& GraphicsSystem::addGraphicsComponent(entt::entity entity, Ogre::String meshName, Ogre::IdString datablockName)
{
    OGRE_ASSERT(m_registry.try_get<SceneComponent>(entity));

    SceneComponent& sceneComponent = m_registry.get<SceneComponent>(entity);

    Ogre::Item* pItem = m_pSceneManager->createItem(meshName, Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME, sceneComponent.pNode->isStatic() ? Ogre::SCENE_STATIC : Ogre::SCENE_DYNAMIC);
    if (datablockName != Ogre::IdString(""))
        pItem->setDatablock(datablockName);

    Ogre::SceneNode* pGraphicsNode = sceneComponent.pNode->createChildSceneNode(sceneComponent.pNode->isStatic() ? Ogre::SCENE_STATIC : Ogre::SCENE_DYNAMIC);
    pGraphicsNode->attachObject(pItem);

    GraphicsComponent& graphicsComponent = m_registry.emplace<GraphicsComponent>(entity);
    graphicsComponent.pItem = pItem;
    return graphicsComponent;
}

void GraphicsSystem::removeGraphicsComponent(entt::entity entity)
{
    const auto& component = m_registry.get<GraphicsComponent>(entity);
    size_t numRemoved = m_registry.remove<GraphicsComponent>(entity);
    OGRE_ASSERT(numRemoved == 1);
}

void GraphicsComponent::setOffset(const Ogre::Vector3& offset)
{
    pItem->getParentNode()->setPosition(offset);
}

void GraphicsComponent::setScale(const Ogre::Vector3& scale)
{
    pItem->getParentNode()->setScale(scale);
}

void GraphicsComponent::setRotation(const Ogre::Quaternion& rot)
{
    pItem->getParentNode()->setOrientation(rot);
}

void GraphicsSystem::handleWindowEvent(SDL_Event& evt)
{
    switch (evt.window.event)
    {
    case SDL_WINDOWEVENT_SIZE_CHANGED:
        int w, h;
        SDL_GetWindowSize(m_pSDLWindow, &w, &h);
#if OGRE_PLATFORM == OGRE_PLATFORM_LINUX
        m_pRenderWindow->requestResolution(w, h);
#endif
        m_pRenderWindow->windowMovedOrResized();
        break;
    case SDL_WINDOWEVENT_RESIZED:
#if OGRE_PLATFORM == OGRE_PLATFORM_LINUX
        mRenderWindow->requestResolution(evt.window.data1, evt.window.data2);
#endif
        m_pRenderWindow->windowMovedOrResized();
        break;
    case SDL_WINDOWEVENT_CLOSE:
        break;
    case SDL_WINDOWEVENT_SHOWN:
        m_pRenderWindow->_setVisible(true);
        break;
    case SDL_WINDOWEVENT_HIDDEN:
        m_pRenderWindow->_setVisible(false);
        break;
    case SDL_WINDOWEVENT_FOCUS_GAINED:
        m_pRenderWindow->setFocused(true);
        break;
    case SDL_WINDOWEVENT_FOCUS_LOST:
        m_pRenderWindow->setFocused(false);
        break;
    }
}

void GraphicsSystem::setWindowTitle(std::string str)
{
    SDL_SetWindowTitle(m_pSDLWindow, str.c_str());
}

bool GraphicsSystem::isWindowVisible() const
{
    return m_pRenderWindow->isVisible();
}

bool GraphicsSystem::isWindowFocused() const
{
    return m_pRenderWindow->isFocused();
}
