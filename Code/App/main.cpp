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

    VrInput& vrInput = Globals::vrInput;
    vrInput.initialize(renderer.getVrSession());

    VRFreeFlyCameraController vrCameraController;
    vrCameraController.initialize(glm::vec3(-1.0f, renderer.isVrStageSpace() ? 0.0f : 1.0f, 0.0f));

    UI& ui = Globals::ui;
    ui.initialize();

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

    World world;
    world.initialize();

    std::vector<EntityPtr> entities;
    entities.push_back(world.spawnAssetFile("Entities/SponzaScene.pre", Transform(glm::vec3(0.0f), 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))));

    GizmoController gizmo;
    gizmo.initialize(world);

    EntityPtr character = world.spawnAssetFile("Entities/character.pre", Transform(glm::vec3(0.0f), 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
    entities.push_back(character);
    float charSpeed = 0.0f;
    Tweak::floatVar("Animation", "Speed", &charSpeed, 0.0f, 1.0f, 0.01f,[&]() { getComponent<AnimatorComponent>(character)->stateMachine.setFloat("speed", charSpeed); });
    if (AnimatorComponent* anim = getComponent<AnimatorComponent>(character))
        anim->onEvent = [](const std::string& e) { Log::info("anim event: " + e); }; // footstep / hit notifies


    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& evt)
        {
            if (evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                if (evt.scancode == SDL_Scancode::SDL_SCANCODE_T) gizmo.setMode(EGizmoMode::Translate);
                if (evt.scancode == SDL_Scancode::SDL_SCANCODE_R) gizmo.setMode(EGizmoMode::Rotate);
                if (evt.scancode == SDL_Scancode::SDL_SCANCODE_G) gizmo.setMode(EGizmoMode::Scale);
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_F5 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                renderer.reloadShaders();
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_F && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                if (AnimatorComponent* anim = getComponent<AnimatorComponent>(character))
                    anim->stateMachine.setTrigger("attack"); // F: one-shot attack (returns to locomotion)
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_P && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                renderer.toggleGiProbeDebug();          // P: show/hide GI probe debug cubes
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_O && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                renderer.cycleGiProbeDebugMode();        // O: cycle irradiance <-> cellSize/LOD color
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_L && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                renderer.setSunLight(-cameraController.getDirection(), glm::vec3(1.0f), 5.0f); // aim the sun along the camera forward
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_1 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.resize(0);
                spawnedLightGeom.resize(0);
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_2 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.push_back(PointLight{ cameraController.getPosition(), 25.0f, glm::abs(glm::sphericalRand(1.0f)), 50.0f });
                spawnedLightGeom.push_back(world.spawn("sphere", Transform(cameraController.getPosition(), 0.1f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_3 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.push_back(PointLight{ cameraController.getPosition(), 15.0f + glm::linearRand(0.5f, 1.5f), glm::abs(glm::sphericalRand(1.0f)), 30.0f });
                spawnedLightGeom.push_back(world.spawn("sphere", Transform(cameraController.getPosition(), 0.1f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_4 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.push_back(SpotLight{ cameraController.getPosition(), 12.5f, glm::vec3(1.0f, 0.95f, 0.8f), 20.0f, cameraController.getDirection(), glm::radians(25.0f), 0.25f });
				glm::quat orientation = cameraController.getOrientation();
				const glm::vec3 camUp = cameraController.getUp();
				const glm::vec3 camRight = glm::normalize(glm::cross(cameraController.getDirection(), camUp));
				orientation = glm::angleAxis(glm::radians(90.0f), camRight) * orientation;
                spawnedLightGeom.push_back(world.spawn("cone", Transform(cameraController.getPosition(), 0.1f, orientation)));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_5 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                const glm::vec3 dir = cameraController.getDirection();
                const glm::vec3 camUp = cameraController.getUp();
                const glm::vec3 up = glm::normalize(camUp);
                const glm::vec3 ref = glm::abs(up.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 right0 = glm::normalize(glm::cross(up, ref));
                const glm::vec3 camRight = glm::normalize(glm::cross(dir, camUp));
                float rotation = atan2f(glm::dot(glm::cross(right0, camRight), up), glm::dot(right0, camRight));
                spawnedLights.push_back(AreaLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 1.0f, 1.0f), 10.0f, camUp, 1.0f, 1.0f, rotation });

                const glm::quat orientation = glm::angleAxis(glm::radians(-90.0f), camRight) * cameraController.getOrientation();
				spawnedLightGeom.push_back(world.spawn("plane", Transform(cameraController.getPosition(), 0.5f, orientation)));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_6 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                const glm::vec3 dir = cameraController.getDirection();
                const glm::vec3 camUp = cameraController.getUp();
                const glm::vec3 up = glm::normalize(camUp);
                const glm::vec3 ref = glm::abs(up.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 right0 = glm::normalize(glm::cross(up, ref));
                const glm::vec3 camRight = glm::normalize(glm::cross(dir, camUp));
                const float rotation = atan2f(glm::dot(glm::cross(right0, camRight), up), glm::dot(right0, camRight));
                spawnedLights.push_back(TubeLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 0.9f, 0.7f), 10.0f, camUp, 0.1f, 1.0f, rotation });
                const glm::quat orientation = glm::angleAxis(glm::radians(-90.0f), camRight) * cameraController.getOrientation();
                spawnedLightGeom.push_back(world.spawn("capsule", Transform(cameraController.getPosition(), 0.5f, orientation)));
            }

            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_7 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                for (int i = 0; i < 100; ++i)
                {
                    int x = 0; int y = 0;
					glm::vec3 position = glm::vec3(x * 30.0f + glm::linearRand(-11.0f, 11.0f), glm::linearRand(0.0f, 7.0f), y * 20.0f + glm::linearRand(-5.0f, 4.5f));
                    spawnedLights.push_back(PointLight{ position,
                        glm::linearRand(0.5f, 2.0f), glm::abs(glm::sphericalRand(1.0f)), glm::linearRand(7.0f, 10.0f) });
                    spawnedLightGeom.push_back(world.spawn("sphere", Transform(position, 0.05f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
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
        renderer.setViewportRect(ui.getViewportRect());

        Camera camera;
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

        for (EntityChange& change : ui.takeEntityChanges())
        {
            if (auto* cv = std::get_if<EntityChange::CreateViewport>(&change.type))
            {
                const glm::vec3 worldPos = camera.screenToWorld(ui.getViewportRect(), cv->screenPos);
                entities.push_back(world.spawnAssetFile(cv->path, Transform(worldPos, 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))));
            }
            else if (auto* ch = std::get_if<EntityChange::CreateHierarchy>(&change.type))
            {
                const Transform base(glm::vec3(0.0f), 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                // Hierarchy drop: keep the asset's authored position/scale offset (don't anchor at base).
                EntityPtr e = world.spawnAssetFile(ch->path, base, false);
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
                if (savePrefab(sp->root.get(), sp->path))
                    world.invalidatePrefab(std::filesystem::path(sp->path).stem().string());
        }

        const Frustum& frustum = renderer.beginFrame(camera);
        for (const EntityPtr& entity : entities)
            entity->renderTree(renderer, Transform(), (float)deltaSec);


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
            gizmo.getGizmoEntity()->renderTree(renderer, Transform());

        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    return 0;
}
