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

import App.InputControls;

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
    renderer.initialize(window, EValidation::ENABLED, EVSync::ENABLED, EVr::DISABLED); // ENABLED DISABLED

    UI& ui = Globals::ui;
    ui.initialize();

    World& world = Globals::world;
    world.initialize();

    PhysicsWorld& physics = Globals::physics;
    physics.initialize();

    AudioSystem& audio = Globals::audio;
    audio.initialize();

    Globals::scriptHost.setCurrentScriptPath("Scripts/Graph.scr");
    ScriptEventManager& scriptEvents = Globals::scriptEvents;
    scriptEvents.initialize();

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
    entities.push_back(world.spawnAssetFile("Entities/SponzaScene.pre", Transform(), false));
    entities.push_back(world.spawnAssetFile("Entities/character.pre", Transform()));

    GizmoController gizmo;
    gizmo.initialize(world);

    InputControls controls(gizmo, cameraController, entities, spawnedLights, spawnedLightGeom, spawnedJoints);

    Camera camera;

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
            if (savePrefab(sp->root.get(), sp->path))
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
            // Entity::create() only stores a raw, non-owning pointer to the template — keep it alive for
            // as long as the entity might reference it (World's own template caches do the same for
            // prefabs; this one is ad-hoc, so nothing else would hold onto it).
            world.keepTemplateAlive(rs->tmpl);

            Transform t(rs->oldEntity->pos, rs->oldEntity->scale, rs->oldEntity->rot);
            EntityPtr newEntity = Entity::create(*rs->tmpl, t);

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
            // SceneComponent::children) — replace it in place so siblings/order aren't disturbed.
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
            Globals::scriptContext.update((float)deltaSec, (float)Globals::time.getElapsedSec(),
                camera.position, camDir, camUp, camera.fovDeg);
            audio.setListener(camera.position, camDir, camUp);
        }

        for (EntityChange& change : scriptEvents.takeEntityChanges())
            handleEntityChange(change);
        for (EntityChange& change : ui.takeEntityChanges())
			handleEntityChange(change);

        physics.update(deltaSec); // fixed-step; entities sync to body poses in their update below
        dispatchPhysicsContactEvents();    // fire onContact hooks + script "Contact/Sensor Begin/End" events

        const Frustum& frustum = renderer.beginFrame(camera);

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

        if (gizmo.isVisible())
            gizmo.getGizmoEntity()->update(renderer, (float)deltaSec);

        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    return 0;
}
