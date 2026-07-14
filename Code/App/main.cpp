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

import App.InputControls;
import Procedural;

int main()
{
    FileSystem::initialize();

    Window window;
    window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(1920, 1080));

    Input& input = Globals::input;
    input.initialize();

    FreeFlyCameraController cameraController;
    cameraController.initialize(glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

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
    Globals::spatialStress.initialize();

    Globals::scriptHost.setCurrentScriptPath("Scripts/Graph.scr");
    ScriptEventManager& scriptEvents = Globals::scriptEvents;
    scriptEvents.initialize();

    // Procedural terrain: bounded ring of camera-following chunks, generated on a worker thread. Declared
    // after the renderer so it's destroyed before it (frees RenderNodes/containers while the device lives).
    Procedural::TerrainStreamer terrain;
    terrain.initialize();

    // Terrain object scattering: biome-driven trees/rocks placed per cell on a worker thread from the
    // same climate field the terrain renders. Declared after the renderer for the same teardown order.
    Procedural::ScatterSystem scatter;
    scatter.initialize();

    // Procedural ocean: one camera-following graded grid, GPU-displaced into Gerstner waves by the Ocean
    // pipeline variant. Declared after the renderer so it frees its RenderNode/container before the device.
    Procedural::OceanRenderer ocean;
    ocean.initialize();
    // Buoyancy: dynamic bodies float in the ocean (keys 8/9's cubes/spheres bob in the swell). The ocean
    // CPU-samples its GPU displacement readback; -FLT_MAX where there is no water gates the whole pass.
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

    std::vector<EntityPtr> entities;
    entities.push_back(world.spawnAssetFile("Entities/sponza.pre", Transform(), false));
    entities.push_back(world.spawnAssetFile("Entities/skysphere.pre", Transform(), false));
    entities.push_back(world.spawnAssetFile("Entities/character.pre", Transform()));

    // Poly Haven PBR vegetation/rock test assets, placed near the camera start for a quick visual check.
    const glm::quat identityQuat(1.0f, 0.0f, 0.0f, 0.0f);
    entities.push_back(world.spawnAssetFile("Entities/namaqualand_rocks_01.pre", Transform(glm::vec3(4.0f, 0.0f, -2.0f), 1.0f, identityQuat)));
    entities.push_back(world.spawnAssetFile("Entities/namaqualand_boulder_04.pre", Transform(glm::vec3(4.0f, 0.0f, 0.0f), 1.0f, identityQuat)));
    entities.push_back(world.spawnAssetFile("Entities/stone_01.pre", Transform(glm::vec3(4.0f, 0.0f, 2.0f), 1.0f, identityQuat)));
    entities.push_back(world.spawnAssetFile("Entities/tree_small_02.pre", Transform(glm::vec3(6.0f, 0.0f, 0.0f), 1.0f, identityQuat)));

    GizmoController gizmo;
    gizmo.initialize(world);

    InputControls controls(gizmo, cameraController, entities, spawnedLights, spawnedLightGeom, spawnedJoints);

    Camera camera;
    glm::dvec3 lastNearQueryPos(1e30); // last camera position the Near visibility ball was stamped at
    uint32 framesSinceNearQuery = 0;

    auto handleEntityChange = [&](EntityChange& change)
    {
        if (auto* cv = std::get_if<EntityChange::CreateViewport>(&change.type))
        {
            const glm::vec3 worldPos = camera.screenToWorld(ui.getViewportRect(), cv->screenPos);
            entities.push_back(world.spawnAssetFile(cv->path, Transform(worldPos, 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))));
        }
        else if (auto* ch = std::get_if<EntityChange::CreateHierarchy>(&change.type))
        {
            EntityPtr e = world.spawnAssetFile(ch->path, Transform(), false);
            if (ch->parent && hasComponent<SceneComponent>(ch->parent))
                e->reparentEntity(ch->parent);   // parent's SceneComponent takes ownership
            else
                entities.push_back(std::move(e));
        }
        else if (auto* as = std::get_if<EntityChange::AddSceneEntity>(&change.type))
        {
            EntityPtr e = world.createEmptyEntity(as->displayName);
            if (as->parent && hasComponent<SceneComponent>(as->parent))
                e->reparentEntity(as->parent);
            else
                entities.push_back(std::move(e));
        }
        else if (auto* del = std::get_if<EntityChange::Delete>(&change.type))
            std::erase_if(entities, [&](const EntityPtr& e) { return e.get() == del->entity.get(); });
        else if (auto* rep = std::get_if<EntityChange::Reparent>(&change.type))
            rep->newParent ? (void)std::erase_if(entities, [&](const EntityPtr& e) { return e.get() == rep->entity.get(); }) : entities.push_back(std::move(rep->entity));
        else if (auto* sp = std::get_if<EntityChange::SavePrefab>(&change.type))
        {
            if (savePrefab(sp->root.get(), sp->path, sp->text))
                world.invalidatePrefab(std::filesystem::path(sp->path).stem().string());
        }
        else if (auto* op = std::get_if<EntityChange::OpenPrefabForEdit>(&change.type))
        {
            EntityPtr e = world.spawnAssetFile(op->path, Transform(), false);
            if (e)
            {
                e->setPrefabInstance(false); // unpack: the Entity Editor edits it freely
                entities.push_back(e);
                ui.onOpened(e, op->path);
            }
        }
        else if (auto* np = std::get_if<EntityChange::NewPrefab>(&change.type))
        {
            EntityPtr e = world.createEmptyEntity(np->displayName);
            entities.push_back(e);
            ui.onOpened(e, "");
        }
        else if (auto* rs = std::get_if<EntityChange::RespawnEntity>(&change.type))
        {
            // Entity::create() only stores a raw, non-owning pointer to the template â€” keep it alive for
            // as long as the entity might reference it (World's own template caches do the same for
            // prefabs; this one is ad-hoc, so nothing else would hold onto it).
            world.keepTemplateAlive(rs->tmpl);

            Transform t(rs->oldEntity->pos, rs->oldEntity->scale, rs->oldEntity->rot);
            // Pre-set the paused flag so component spawn (script OnSpawn) already sees it â€” the parent
            // chain isn't linked yet during create, so a paused ancestor can't be discovered there.
            const uint8 initialFlags = rs->oldEntity->isEditorPausedInTree() ? uint8(EEntityFlag_EditorPaused) : uint8(0);
            EntityPtr newEntity = Entity::create(*rs->tmpl, t, initialFlags);
            newEntity->setPrefabInstance(rs->oldEntity->isPrefabInstance()); // keep the editor's unpacked state despite the template's prefabName

            // Preserve any existing children (the Entity Editor commits one entity's own component set at
            // a time; whatever was already parented under it stays put).
            if (SceneComponent* oldSc = getComponent<SceneComponent>(rs->oldEntity.get()))
                if (SceneComponent* newSc = getComponent<SceneComponent>(newEntity.get()))
                {
                    newSc->children = std::move(oldSc->children);
                    for (EntityPtr& child : newSc->children)
                        child->parent = newEntity.get();
                }

            // Re-attach where the old entity was: a root (in `entities`) or a child (in its parent's
            // SceneComponent::children) â€” replace it in place so siblings/order aren't disturbed.
            if (Entity* parent = rs->oldEntity->parent)
            {
                newEntity->parent = parent;
                if (SceneComponent* parentSc = getComponent<SceneComponent>(parent))
                    for (EntityPtr& child : parentSc->children)
                        if (child.get() == rs->oldEntity.get())
                        {
                            child = newEntity;
                            break;
                        }
            }
            else
            {
                std::erase_if(entities, [&](const EntityPtr& e) { return e.get() == rs->oldEntity.get(); });
                entities.push_back(newEntity);
            }

            ui.onEntityRespawned(rs->oldEntity, newEntity);
        }
    };

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
        ui.update(entities, deltaSec);

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

        // Refresh the script ABI's per-frame fields (delta/elapsed seconds, camera) once before any script
        // runs this frame. Forward/up are derived from the inverse view matrix (its -Z / Y columns).
        {
            const glm::mat4 camToWorld = glm::inverse(camera.viewMatrix);
            const glm::vec3 camDir = glm::normalize(-glm::vec3(camToWorld[2]));
            const glm::vec3 camUp = glm::normalize(glm::vec3(camToWorld[1]));
            Globals::scriptContext.update(camera, (float)deltaSec, (float)Globals::time.getElapsedSec());
            audio.setListener(camera.position, camDir, camUp);
        }

        for (EntityChange& change : scriptEvents.takeEntityChanges())
            handleEntityChange(change);
        for (EntityChange& change : ui.takeEntityChanges())
			handleEntityChange(change);

        physics.update(deltaSec); // fixed-step; entities sync to body poses in their update below
        dispatchPhysicsContactEvents();    // fire onContact hooks + script "Contact/Sensor Begin/End" events

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
        Globals::spatialStress.update(glm::dvec3(camera.position), frustum);

        for (const EntityPtr& entity : entities)
            entity->update(renderer, (float)deltaSec);

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

        terrain.update(renderer, camera);
        // The terrain's height field feeds the ocean's shore-depth bake (shoaling + surf at the coast)
        // and drives the object scattering (both clear themselves while terrain is disabled).
        ocean.update(renderer, camera, terrain.activeClimateMaps(), terrain.activeWaterReach(), terrain.seaLevel());
        scatter.update(renderer, camera, terrain.activeClimateMaps());

        if (gizmo.isVisible())
            gizmo.getGizmoEntity()->update(renderer, (float)deltaSec);

        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    physics.setWaterSurface({}); // the callback captures the stack-local ocean; drop it before it dies
    return 0;
}
