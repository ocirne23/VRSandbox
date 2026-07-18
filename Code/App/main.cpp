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
    renderer.initialize(window, EValidation::ENABLED, EVSync::DISABLED, EVr::DISABLED); // ENABLED DISABLED

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

    Globals::scriptHost.setCurrentScriptPath("Scripts/Graph.scr");
    ScriptEventManager& scriptEvents = Globals::scriptEvents;
    scriptEvents.initialize();

    Procedural::TerrainStreamer terrain;
    terrain.initialize();

	EntityPtr terrainEntity = world.createEmptyEntity("Terrain");
    Procedural::TerrainCollider terrainCollider;
    terrainCollider.initialize((void*)terrainEntity);

    Procedural::ScatterSystem scatter;
    scatter.initialize();

    Procedural::OceanGenerator ocean;
    ocean.initialize();

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

    std::vector<RendererVKLayout::LightInfo> spawnedLights;
    std::vector<EntityPtr> spawnedLightGeom;
    std::vector<PhysicsJoint> spawnedJoints;

    const glm::vec3 spawnOffset = spawnPos - glm::vec3(0, 1, 1);
    world.addRootEntity(world.spawnAssetFile("Entities/sponza.pre", Transform(spawnOffset), true));
    world.addRootEntity(world.spawnAssetFile("Entities/skysphere.pre", Transform(spawnOffset), true));
    world.addRootEntity(world.spawnAssetFile("Entities/character.pre", Transform(spawnOffset), true));
    world.addRootEntity(world.spawnAssetFile("Entities/particle.pre", Transform(spawnOffset), true));

    GizmoController gizmo;
    gizmo.initialize(world);

    InputControls controls(gizmo, cameraController, world, spawnedLights, spawnedLightGeom, spawnedJoints);

    Camera camera;
    glm::dvec3 lastNearQueryPos(1e30); // last camera position the Near visibility ball was stamped at
    uint32 framesSinceNearQuery = 0;

    // World owns the root entities and applies all queued changes; the UI notifications it can't
    // deliver itself (dependency points UI -> Entity) route back through these.
    world.setOnPrefabOpened([&ui](const EntityPtr& entity, const std::string& path) { ui.onOpened(entity, path); });
    world.setOnEntityRespawned([&ui](const EntityPtr& oldEntity, const EntityPtr& newEntity) { ui.onEntityRespawned(oldEntity, newEntity); });
    auto handleEntityChange = [&](EntityChange& change) { world.handleEntityChange(change, camera, ui.getViewportRect()); };

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
        cameraController.update(deltaSec);
        ui.update(world.rootEntities(), deltaSec);

        for (const std::string& reloadPath : ui.takeScriptReloadRequests())
			Globals::scriptHost.getOrLoad(reloadPath, true);

        renderer.setViewportRect(ui.getViewportRect());

        if (renderer.isVrEnabled())
        {
            vrCameraController.update(deltaSec); // thumbstick locomotion; pulls Globals::vrInput
            camera = vrCameraController.getCamera();
        }
        else
        {
            camera = cameraController.getCamera();
        }

        gizmo.update(camera, ui.getViewportRect(), ui.getSelectedEntity(), deltaSec);
        audio.setListener(camera);
        
        for (EntityChange& change : scriptEvents.takeEntityChanges()) handleEntityChange(change);
        for (EntityChange& change : ui.takeEntityChanges()) handleEntityChange(change);

        Globals::scriptContext.update(camera, (float)deltaSec, (float)Globals::time.getElapsedSec());
        physics.update(deltaSec, [&](const PhysicsWorld::ContactEvent& evt) { world.handleContactEvent(evt); }); // fixed-step; entities sync to body poses in their update below

        // Collider wireframes ("Physics/Debug/Draw colliders" tweak): box3d debug draw decomposed into
        // world-space lines, pushed to the renderer's per-frame debug line overlay.
        if (physics.isDebugDrawEnabled())
            physics.debugDraw(camera.position, [&renderer](const glm::vec3& a, const glm::vec3& b, uint32 color) {
                renderer.addDebugLine(a, b, color);
            });

        const Frustum& frustum = renderer.beginFrame(camera);

        spatialIndex.commitFrame(); // applies cell moves queued during last frame's entity updates
        spatialIndex.setCullMaxDist(camera.far); // cull to exactly the view distance, not a fixed cap
        const SpatialCullingConfig& cullingConfig = spatialIndex.getCullingConfig();
        if (cullingConfig.mode != int(ESpatialCullMode::Off) && !cullingConfig.freeze)
        {
            const glm::dvec3 cameraPos = glm::dvec3(camera.position);
            Frustum cullFrustum = rebaseFrustum(frustum, cameraPos);
            inflateFrustum(cullFrustum, cullingConfig.margin);
            IOcclusionTester* occlusion = nullptr;
            if (Globals::occlusionBuffer.isEnabled())
            {
                // The renderer's projection is REVERSED-Z; the CPU occlusion rasterizer assumes NDC z grows
                // with distance (farthest-depth blocks, huge "no occluder" sentinel). Flip the z row back to
                // standard orientation (z' = w - z) so its internal comparisons keep their meaning.
                glm::mat4 viewProjRelCamera = renderer.getCenterViewProj() * glm::translate(glm::mat4(1.0f), camera.position);
                for (int c = 0; c < 4; ++c)
                    viewProjRelCamera[c][2] = viewProjRelCamera[c][3] - viewProjRelCamera[c][2];
                Globals::occlusionBuffer.render(viewProjRelCamera, cameraPos);
                occlusion = &Globals::occlusionBuffer;
            }
            // Terrain chunks ride the Main stamp too (TerrainStreamer registers them on their own layer);
            // they skip the Near ball — main-culled terrain keeps its shadow/GI passes unconditionally.
            spatialIndex.markVisibleSet(ESpatialPass::Main, cullFrustum, cameraPos,
                cullingConfig.maxDist + cullingConfig.margin, SpatialLayer_Render | SpatialLayer_Terrain, occlusion);
            // The Near ball barely changes frame to frame: inflate it by the slack and requery only
            // once the camera has moved that far. The frame cap keeps off-screen MOVERS from staying
            // unstamped too long (they can enter the ball without the camera moving).
            const float nearSlack = cullingConfig.nearSlack;
            ++framesSinceNearQuery;
            if (nearSlack <= 0.0f || glm::distance(lastNearQueryPos, cameraPos) > double(nearSlack) || framesSinceNearQuery >= 30)
            {
                spatialIndex.markVisibleSphere(ESpatialPass::Near, cameraPos, cullingConfig.nearRadius + nearSlack, SpatialLayer_Render);
                lastNearQueryPos = cameraPos;
                framesSinceNearQuery = 0;
            }
        }

        world.update(renderer, (float)deltaSec); // serial script prepass + parallel component/tree pass + sink flush

		for (auto& light : spawnedLights)
		{
			renderer.addLightInfo(light);
		}

        for (const EntityPtr& entity : spawnedLightGeom)
        {
            if (!entity)
                continue;
            RenderComponent* render = getComponent<RenderComponent>(entity);
            if (render && frustum.sphereInFrustum(render->node.getWorldBounds()))
                renderer.renderNode(render->node);
        }

        // The baked flow directions ease back to the swell's travel heading offshore; the ocean owns that
        // angle, the terrain owns the flow rule — wire the one value across so both maps bake the same field.
        terrain.setFlowWindAngle(ocean.swellTravelAngle());
        terrain.update(renderer, camera);
        // ... and the collider tiles under/around the camera (clears itself while terrain is disabled).
        terrainCollider.update(camera.position, terrain.activeClimateMaps());
        // The terrain's baked data map feeds the ocean's water depth/level (shoaling + surf at the
        // coast, buoyancy) and its height field drives the object scattering (both clear themselves
        // while terrain is disabled).
        ocean.update(renderer, camera, terrain.activeTerrainData(), terrain.seaLevel());
        scatter.update(renderer, camera, terrain.activeClimateMaps());

        // Particle effects + decals: turns emitter rates/bursts into GPU spawn requests and submits the
        // live decal set (after world.update so effects follow this frame's entity transforms).
        particleSystem.update(renderer, (float)deltaSec);

        // Forcefield bubbles: test force-balls follow their bodies + feed the read-back force into
        // physics, then the system pushes live emitter state + query positions and latches readbacks.
        controls.update((float)deltaSec);
        forceSystem.update(renderer, (float)deltaSec);

        if (gizmo.isVisible())
            gizmo.getGizmoEntity()->update(renderer, (float)deltaSec);

        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    physics.setWaterSurface({}); // the callback captures the stack-local ocean; drop it before it dies
    // World is a global: without this, the roots would die during static teardown, where the
    // renderer/physics globals they release into may already be gone (cross-library order is undefined).
    world.clearRootEntities();
    jobSystem.shutdown(); // join the workers while every global they might touch still lives
    return 0;
}
