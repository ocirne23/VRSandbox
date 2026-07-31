import Core;
import Core.Allocator;
import Core.Log;
import Core.Window;
import Core.SDL;
import Core.Frustum;
import Core.Time;
import Core.glm;
import Core.Camera;
import Core.Tweaks;

import Animation;
import File;
import Input;
import UI;
import RendererVK;
import Entity;
import Script;
import Physics;
import Audio;
import Spatial;
import Threading;

import App.InputControls;
import Procedural;
import Particle;
import Force;

int main()
{
    FileSystem::initialize();

    Window window;
    window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(1920, 1080));

    Input& input = Globals::input;
    input.initialize();

    JobSystem& jobSystem = Globals::jobSystem;
    jobSystem.initialize();

    const glm::vec3 spawnPos = glm::vec3(50.0f, 80.0f, 2327.0f);
    FreeFlyCameraController cameraController;
    cameraController.initialize(spawnPos, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Renderer& renderer = Globals::rendererVK;
    renderer.initialize(window, EValidation::DISABLED, EVSync::ENABLED, EVr::DISABLED); // ENABLED DISABLED

    UI& ui = Globals::ui;
    ui.initialize();

    World& world = Globals::world;
    world.initialize();

    PhysicsWorld& physics = Globals::physics;
    physics.initialize();

    AudioSystem& audio = Globals::audio;
    audio.initialize();

    SpatialIndex& spatialIndex = Globals::spatialIndex;
    spatialIndex.initialize();
    Globals::occlusionBuffer.initialize();

    ParticleSystem& particleSystem = Globals::particleSystem;
    particleSystem.initialize(); // before any world spawn: ParticleComponents register effects on spawn

    ForceSystem& forceSystem = Globals::forceSystem;
    forceSystem.initialize();

    ScriptContext& scriptContext = Globals::scriptContext;
    ScriptHost& scriptHost = Globals::scriptHost;
    scriptHost.setCurrentScriptPath("Scripts/Graph.scr");

    ScriptEventManager& scriptEvents = Globals::scriptEvents;
    scriptEvents.initialize();
    registerScriptDslBindings(); // must run before anything touches Globals::scriptBindings (ScriptEditor's build() included)

    Procedural::TerrainStreamer terrain;
    terrain.initialize();

	EntityPtr terrainEntity = world.createEmptyEntity("Terrain");
    Procedural::TerrainCollider terrainCollider;
    terrainCollider.initialize((void*)terrainEntity);

    Procedural::ScatterSystem scatter;
    scatter.initialize();

    Procedural::OceanGenerator ocean;
    ocean.initialize();
    terrain.setFlowWindAngle(ocean.swellTravelAngle());
    physics.setWaterSurface([&ocean](float x, float z) { return ocean.sampleWaterHeight(x, z); });

    VrInput& vrInput = Globals::vrInput;
    VRFreeFlyCameraController vrCameraController;

    if (renderer.isVrEnabled())
    {
        vrInput.initialize(renderer.getVrSession());
        vrCameraController.initialize(glm::vec3(-1.0f, renderer.isVrStageSpace() ? 0.0f : 1.0f, 0.0f));
    }

    bool running = true;
    SystemEventListener* pSystemEventListener = input.addSystemEventListener();
    pSystemEventListener->onQuit = [&]() { running = false; };
    pSystemEventListener->onWindowEvent = [&](const SDL_WindowEvent& evt)
        {
            if (evt.type == SDL_EVENT_WINDOW_RESIZED)   renderer.recreateWindowSurface(window);
            if (evt.type == SDL_EVENT_WINDOW_MINIMIZED) renderer.setWindowMinimized(true);
            if (evt.type == SDL_EVENT_WINDOW_MAXIMIZED) renderer.setWindowMinimized(false);
            if (evt.type == SDL_EVENT_WINDOW_RESTORED)  renderer.setWindowMinimized(false);
        };

    KeyboardListener* pKeyboardListener = input.addKeyboardListener();

    std::vector<EntityPtr> spawnedLights; // test lights (keys 1-7); they update + render like any entity
    std::vector<PhysicsJoint> spawnedJoints;

    const glm::vec3 spawnOffset = spawnPos - glm::vec3(0, 1, 1);
    //world.addRootEntity(world.spawnAssetFile("Entities/sponza.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/skysphere.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/character.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/particle.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/SphereField.pre", Transform(spawnOffset), true));

    GizmoController gizmo;
    gizmo.initialize(world);
    ui.setGizmo(&gizmo);

    InputControls controls(gizmo, cameraController, world, spawnedLights, spawnedJoints);

    Camera camera;
    physics.setDebugDrawCallback([&renderer](const glm::vec3& a, const glm::vec3& b, uint32 color) { renderer.addDebugLine(a, b, color); }, [&camera]() { return camera.position; });

    world.setOnPrefabOpened([&ui](const EntityPtr& entity, const std::string& path) { ui.onOpened(entity, path); });
    world.setOnEntityRespawned([&ui](const EntityPtr& oldEntity, const EntityPtr& newEntity) { ui.onEntityRespawned(oldEntity, newEntity); });

    uint32 frameCount = 0;
    uint32 fps = 0;
    Timer fpsTimer(std::chrono::seconds(1), [&](Timer& timer) {
            fps = frameCount;
            frameCount = 0;
            return Timer::REPEAT;
        });
    
    Timer titleUpdateTimer(std::chrono::milliseconds(100), [&](Timer& timer) {
            glm::vec3 pos = cameraController.getPosition();
            glm::vec3 dir = cameraController.getDirection();
            char windowTitleBuf[256];
            sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "FPS: %i mem: %.2fmb instances: %i meshtypes: %i materials: %i lights: %i, pos: %.1f, %.1f, %.1f, dir: %.1f, %.1f, %.1f",
                fps, (double)(Globals::allocator.getUsedSize() + getAlignedAllocatedSize()) / 1024.0 / 1024.0,
                renderer.getNumMeshInstances(), renderer.getNumMeshTypes(), renderer.getNumMaterials(), (int)spawnedLights.size(), pos.x, pos.y, pos.z, dir.x, dir.y, dir.z);
            window.setTitle(windowTitleBuf);
            return Timer::REPEAT;
        });

    Timer renderStatsUpdateTimer(std::chrono::seconds(1), [&](Timer& timer) {
		ui.setRenderStats(renderer.getStats());
        return Timer::REPEAT;
    });

    while (running)
    {
        Globals::time.update();
        const double deltaSec = Globals::time.getDeltaSec();

        input.update(deltaSec);
        controls.update((float)deltaSec);
        ui.update(world.rootEntities(), camera, deltaSec); // also drives the gizmo it owns

        for (const std::string& reloadPath : ui.takeScriptReloadRequests()) scriptHost.getOrLoad(reloadPath, true);
        for (EntityChange& change : scriptEvents.takeEntityChanges()) world.handleEntityChange(change, camera, ui.getViewportRect());
        for (EntityChange& change : ui.takeEntityChanges())           world.handleEntityChange(change, camera, ui.getViewportRect());

        scriptContext.update(camera, (float)deltaSec, (float)Globals::time.getElapsedSec());
        audio.update(camera);
        physics.update(deltaSec, [&](const PhysicsWorld::ContactEvent& evt) { world.handleContactEvent(evt); });

        const Frustum& frustum = renderer.beginFrame(camera, ui.getViewportRect());
        spatialIndex.update(camera, frustum, renderer.getCenterViewProj() * glm::translate(glm::mat4(1.0f), camera.position)); // translate corrects the reverse-z renderer proj matrix
        world.update(renderer, (float)deltaSec); // serial script prepass + parallel component/tree pass + sink flush
        terrain.update(renderer, camera);
        terrainCollider.update(camera.position, terrain.activeClimateMaps());
        ocean.update(renderer, camera, terrain.activeTerrainData(), terrain.seaLevel());
        scatter.update(renderer, camera, terrain.activeClimateMaps());
        particleSystem.update(renderer, (float)deltaSec);
        forceSystem.update(renderer, (float)deltaSec);

        ui.drawGizmoEntity(renderer, (float)deltaSec);
        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    spawnedLights.clear(); // released before the World's roots: entities must not outlive the globals
    world.clearRootEntities();
    jobSystem.shutdown();
    return 0;
}
