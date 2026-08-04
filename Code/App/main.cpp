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
import Core.Windows;

static std::atomic<bool> g_headlessRunning = true;
static BOOL __stdcall headlessCtrlHandler(DWORD) { g_headlessRunning = false; return TRUE; } // any console ctrl event = clean shutdown

// Server: per-player entities. Each joining client gets a claim-driven cube it OWNS — on the owning
// client, gizmo-dragging it is authoritative movement (streamed as claims, validated server-side;
// drag implausibly fast and the server rejects + force-corrects). Torn down when the client leaves;
// the despawn replicates to everyone else. Main thread (fired from networkManager.receive).
static void setupServerJoinCallbacks()
{
    Globals::networkManager.setOnClientJoined([](uint32 clientId)
        {
            World& world = Globals::world;
            EntityPtr player = world.spawnAssetFile("Entities/Debug/netPlayerCube.pre",
                Transform(glm::vec3(0, 10.0f, 0)), true);
            if (!player)
                return;
            player->setName("Player " + std::to_string(clientId));
            Globals::networkManager.setOwner(*player, clientId);
            // ownership STEALING: whatever this player's body collides with becomes theirs (last
            // collider wins, other players' primaries excluded) — needs ContactEvents on the shapes
            if (PhysicsComponent* pc = getComponent<PhysicsComponent>(player.get()))
                pc->onContact = [clientId](Entity& other, bool begin)
                {
                    if (begin)
                        Globals::networkManager.stealOwnershipOnContact(other, clientId);
                };
            world.addRootEntity(std::move(player));
        });
    Globals::networkManager.setOnClientLeft([](uint32 clientId)
        {
            World& world = Globals::world;
            std::vector<Entity*> owned; // collected first: removeRootEntity mutates the list being walked
            for (const EntityPtr& root : world.rootEntities())
                if (const NetworkComponent* comp = getComponent<NetworkComponent>(root.get()); comp && comp->ownerClientId == clientId)
                    owned.push_back(root.get());
            for (Entity* entity : owned)
                world.removeRootEntity(entity);
        });
}

// Dedicated server without a window, renderer, UI or input. World::setHeadless makes every template
// carry only Scene/Physics/Script/Network components, so nothing ever touches the uninitialized
// renderer global (it exists — static init_seg — but initialize never runs) and no GPU resource is
// created; Hull/Mesh colliders still build from the renderer-free collision import. Deliberately
// skipped for now (documented divergences from a rendering server): terrain/ocean/scatter and the
// terrain collider (camera-centered streaming — a dedicated server needs per-player rings, future
// work; the test scene carries its own ground), buoyancy (reads the GPU ocean), audio, spatial
// entries (only RenderComponents register, so script radius queries see nothing server-side).
static int runHeadlessServer(uint16 netPort, int tickHz)
{
    ProfileScope initScope("headless initialize", EProfileCategory::App);

    FileSystem::initialize();
    Globals::jobSystem.initialize();
    Globals::world.initialize();
    Globals::world.setHeadless(true); // BEFORE any spawn: gates template building
    Globals::physics.initialize();
    Globals::spatialIndex.initialize();    // stays empty, but script spatial queries must be safe to run
    Globals::occlusionBuffer.initialize(); // static mesh colliders register occluders at spawn
    Globals::scriptEvents.initialize();
    registerScriptDslBindings();

    NetworkManager& networkManager = Globals::networkManager;
    networkManager.registerTweaks(); // no tweak UI headless; registering keeps one init path and is harmless
    if (!networkManager.startServer(netPort))
    {
        Globals::jobSystem.shutdown();
        return 1;
    }
    setupServerJoinCallbacks();

    World& world = Globals::world;
    world.addRootEntity(world.spawnAssetFile("Entities/Debug/networkTest.pre", Transform(glm::vec3(0, 0, 0)), true));

    SetConsoleCtrlHandler(&headlessCtrlHandler, TRUE);
    Log::info("Headless server running at " + std::to_string(tickHz) + " Hz - Ctrl+C to stop");
    initScope.stop();

    Camera camera; // scripts read a camera (position/sun queries) — headless has no real one, divergence accepted
    camera.viewMatrix = glm::mat4(1.0f); // members have no default initializers (garbage would NaN the script camera thunks)
    camera.position = glm::vec3(0.0f);
    uint32 tickCount = 0;
    Timer statusTimer(std::chrono::seconds(5), [&](Timer&) {
        Log::info("Headless: " + std::to_string(tickCount / 5) + " ticks/s | " + networkManager.getStatusText());
        tickCount = 0;
        return Timer::REPEAT;
    });

    const std::chrono::microseconds tickInterval(uint64(1000000.0 / glm::clamp(tickHz, 10, 240)));
    while (g_headlessRunning)
    {
        const auto frameStart = std::chrono::steady_clock::now();
        {
            ProfileScope mainLoopScope("main loop", EProfileCategory::App);

            Globals::time.update();
            const double deltaSec = Globals::time.getDeltaSec();

            for (EntityChange& change : Globals::scriptEvents.takeEntityChanges())
                world.handleEntityChange(change, camera, Rect());

            networkManager.receive(deltaSec);
            Globals::scriptContext.update(camera, (float)deltaSec, (float)Globals::time.getElapsedSec());
            Globals::physics.update(deltaSec, [&world](const PhysicsWorld::ContactEvent& evt) { world.handleContactEvent(evt); });
            world.update(Globals::rendererVK, (float)deltaSec); // renderer passed through but never dereferenced (headless archetypes)
            networkManager.send(deltaSec);
            ++tickCount;
        }
        Globals::profiler.endFrame();

        // coarse Sleep-based tick limiter: precision doesn't matter — the physics accumulator and the
        // snapshot accumulator both absorb jitter, this only stops the loop from spinning a core
        while (g_headlessRunning && std::chrono::steady_clock::now() - frameStart < tickInterval)
            Sleep(1);
    }

    Log::info("Headless server shutting down");
    world.clearRootEntities(); // NetworkComponents unregister here, before the manager closes its host
    networkManager.shutdown();
    Globals::jobSystem.shutdown();
    return 0;
}

int main(int argc, char* argv[])
{
    Globals::profiler.endStaticInit(); // closes the "Static init" scope opened at the profiler's static-init construction; must precede any main() scope

    // --server [--port N] [--headless] [--tickrate N] hosts an authoritative session; --connect
    // <ip[:port]> joins one; no flags = single player (networking fully inert). Same executable for
    // all of them; --headless runs the server without window/renderer/UI (server only).
    enum class ELaunchMode { Single, Server, Client };
    ELaunchMode launchMode = ELaunchMode::Single;
    uint16 netPort = 27888;
    int tickHz = 60;
    std::string connectAddress;
    bool headless = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--server")                            launchMode = ELaunchMode::Server;
        else if (arg == "--connect" && i + 1 < argc)      { launchMode = ELaunchMode::Client; connectAddress = argv[++i]; }
        else if (arg == "--port" && i + 1 < argc)         netPort = uint16(std::atoi(argv[++i]));
        else if (arg == "--tickrate" && i + 1 < argc)     tickHz = std::atoi(argv[++i]);
        else if (arg == "--headless")                     headless = true;
        else Log::warning("Unknown command line argument: " + std::string(arg));
    }
    if (headless && launchMode == ELaunchMode::Server)
        return runHeadlessServer(netPort, tickHz);
    if (headless)
        Log::warning("--headless only applies to --server, ignoring");

    ProfileScope initScope("main() initialize", EProfileCategory::App);

    FileSystem::initialize();

    Profiler& profiler = Globals::profiler;  // Profiler + MemoryTracker self-initialize at static init

    Window window;
    window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(1920, 1080));

    Input& input = Globals::input;
    input.initialize();

    JobSystem& jobSystem = Globals::jobSystem;
    jobSystem.initialize();

    const glm::vec3 spawnPos = glm::vec3(-1.5f, 14.0f, -7.1f);
    FreeFlyCameraController cameraController;
    cameraController.initialize(spawnPos, glm::vec3(0.0f, 4.0f, 0.0f));

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

    NetworkManager& networkManager = Globals::networkManager;
    networkManager.registerTweaks(); // the "Network" tweak section exists in single player too
    if (launchMode != ELaunchMode::Single)
    {
        const bool started = launchMode == ELaunchMode::Server
            ? networkManager.startServer(netPort)
            : networkManager.startClient(connectAddress, netPort);
        if (!started)
        {
            jobSystem.shutdown();
            return 1;
        }
        if (launchMode == ELaunchMode::Server)
            setupServerJoinCallbacks();
    }

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

    //world.addRootEntity(world.spawnAssetFile("Entities/sponza.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/skysphere.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/character.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/particle.pre", Transform(spawnOffset), true));
    //world.addRootEntity(world.spawnAssetFile("Entities/SphereField.pre", Transform(spawnOffset), true));

    if (launchMode == ELaunchMode::Server)
    {
        // networked entities are SERVER-OWNED: only the server places the shared scene — its Network
        // components register, get server ids, and materialize on every client via Spawn replication
        // (a client's own scene can be arbitrarily different; it spawns nothing shared)
        world.addRootEntity(world.spawnAssetFile("Entities/Debug/networkTest.pre", Transform(glm::vec3(0, 0, 0)), true));
    }

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
            const std::string netStatus = networkManager.getStatusText(); // empty in single player
            char windowTitleBuf[320];
            sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "%s%sFPS: %i mem: %.2fmb instances: %i meshtypes: %i materials: %i lights: %i, pos: %.1f, %.1f, %.1f, dir: %.1f, %.1f, %.1f",
                netStatus.c_str(), netStatus.empty() ? "" : " | ",
                fps, (double)(Globals::allocator.getUsedSize() + getAlignedAllocatedSize()) / 1024.0 / 1024.0,
                renderer.getNumMeshInstances(), renderer.getNumMeshTypes(), renderer.getNumMaterials(), (int)spawnedLights.size(), pos.x, pos.y, pos.z, dir.x, dir.y, dir.z);
            window.setTitle(windowTitleBuf);
            return Timer::REPEAT;
        });

    Timer renderStatsUpdateTimer(std::chrono::seconds(1), [&](Timer& timer) {
		ui.setRenderStats(renderer.getStats());
        return Timer::REPEAT;
    });

    initScope.stop();

    while (running)
    {
        ProfileScope mainLoopScope("main loop", EProfileCategory::App);

        Globals::time.update();
        const double deltaSec = Globals::time.getDeltaSec();

        input.update(deltaSec);
        controls.update((float)deltaSec);
        if (renderer.isVrEnabled())
        {
            vrCameraController.update(deltaSec); // thumbstick locomotion; pulls Globals::vrInput
            camera = vrCameraController.getCamera();
        }
        else
        {
            cameraController.update(deltaSec);
            camera = cameraController.getCamera();
        }
        ui.update(world.rootEntities(), camera, deltaSec); // also drives the gizmo it owns

        for (const std::string& reloadPath : ui.takeScriptReloadRequests()) scriptHost.getOrLoad(reloadPath, true);
        for (EntityChange& change : scriptEvents.takeEntityChanges()) world.handleEntityChange(change, camera, ui.getViewportRect());
        for (EntityChange& change : ui.takeEntityChanges())           world.handleEntityChange(change, camera, ui.getViewportRect());

        networkManager.receive(deltaSec); // snapshot targets + events land before the sim/entity updates read them

        scriptContext.update(camera, (float)deltaSec, (float)Globals::time.getElapsedSec());
        audio.update(camera);
        physics.update(deltaSec, [&](const PhysicsWorld::ContactEvent& evt) { world.handleContactEvent(evt); });

        const Frustum& frustum = renderer.beginFrame(camera, ui.getViewportRect());
        spatialIndex.update(camera, frustum, renderer.getCenterViewProj() * glm::translate(glm::mat4(1.0f), camera.position)); // translate corrects the reverse-z renderer proj matrix
        world.update(renderer, (float)deltaSec); // serial script prepass + parallel component/tree pass + sink flush
        networkManager.send(deltaSec); // server: snapshot entities at their post-update poses; both roles: flush queued packets
        terrain.update(renderer, camera);
        terrainCollider.update(camera.position, terrain.activeClimateMaps());
        ocean.update(renderer, camera, terrain.activeTerrainData(), terrain.seaLevel());
        scatter.update(renderer, camera, terrain.activeClimateMaps());
        particleSystem.update(renderer, (float)deltaSec);
        forceSystem.update(renderer, (float)deltaSec);

        ui.drawGizmoEntity(renderer, (float)deltaSec);
        ui.render();
        renderer.present();

        mainLoopScope.stop(); // before the frame mark, so the record stays inside this frame's window
        profiler.endFrame();
        frameCount++;
    }

    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    spawnedLights.clear(); // released before the World's roots: entities must not outlive the globals
    world.clearRootEntities(); // NetworkComponents unregister here, before the manager closes its host
    networkManager.shutdown();
    jobSystem.shutdown();
    return 0;
}
