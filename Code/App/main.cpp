import Core;
import Core.Allocator;
import Core.Window;
import Core.SDL;
import Core.Frustum;
import Core.Time;
import Core.glm;
import Core.Camera;

import File.FileSystem;
import File.ISceneData;
import Input;
import Input.VrInput;
import Input.FreeFlyCameraController;
import Input.VRFreeFlyCameraController;
import UI;

import RendererVK;

int main()
{
    FileSystem::initialize();

    Window window;
    window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(1920, 1080));

    Input& input = Globals::input;
    input.initialize();

    FreeFlyCameraController cameraController;
    cameraController.initialize(glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    VRFreeFlyCameraController vrCameraController;
    vrCameraController.initialize(glm::vec3(-1.0f, 1.0f, 0.0f));

    Renderer& renderer = Globals::rendererVK;
    renderer.initialize(window, EValidation::ENABLED, EVSync::DISABLED, EVr::DISABLED); // ENABLED DISABLED

    // VR controller input lives in the Input lib but OpenXR is owned by the renderer; bridge the handles
    // here so the two libs stay decoupled. No-op (falls back to desktop) when VR isn't active.
    VrInput& vrInput = Globals::vrInput;
    vrInput.initialize(renderer.getVrSession());

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

    std::vector<RenderNode> spawnedNodes;
    std::vector<RendererVKLayout::LightInfo> spawnedLights;
    std::vector<RenderNode> spawnedLightGeom;

    ObjectContainer container;
    ObjectContainer baseShapes;
    ObjectContainer skySphere;
    ObjectContainer container2;
    ObjectContainer container3;
    const int spawnCountX = 1;
    const int spawnCountY = 1;
    RenderNode sunLightNode;
    {
        /*
        std::unique_ptr<ISceneData> sceneData = ISceneData::createProceduralLoader();
        sceneData->initialize("terrain", true, true);
        container2.initialize(*sceneData);
        spawnedNodes.push_back(container2.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(glm::vec3(0.0f, -5.0f, 0.0f), 100.0f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
        */
    }
    
    {
        std::unique_ptr<ISceneData> sceneData = ISceneData::createAssimpLoader();
        sceneData->initialize("Models/sponza.glb", true, false);
        container.initialize(*sceneData);
        
        // flip upside down
        glm::quat rot = glm::rotate(glm::quat(1.0, 0.0, 0.0, 0), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        for (int x = 0; x < spawnCountX; ++x)
            for (int y = 0; y < spawnCountY; ++y)
                spawnedNodes.push_back(container.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(glm::vec3(x * 30.0f, 0, y * 20.0f), 1.0f, glm::normalize(rot))));
    }
    /*
    {
        std::unique_ptr<ISceneData> sceneData = ISceneData::createAssimpLoader();
        sceneData->initialize("Models/dragon.glb", false, false);
        container3.initialize(*sceneData);
        for (int x = 0; x < spawnCountX; ++x)
            for (int y = 0; y < spawnCountY; ++y)
                spawnedNodes.push_back(container3.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(glm::vec3(x * 30.0f, 2.0f, y * 20.0f), 1.0f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
    }*/
    
    {
       std::unique_ptr<ISceneData> sceneData = ISceneData::createAssimpLoader();
       sceneData->initialize("Models/baseshapes.glb", false, false);
       ObjectContainer::MaterialOverrides overrides;
       overrides.diffuseTexIdx = RendererVKLayout::FALLBACK_DIFFUSE_TEX_IDX;
       overrides.normalTexIdx = RendererVKLayout::FALLBACK_NORMAL_TEX_IDX;
       overrides.metalRoughnessTexIdx = UINT16_MAX;
       overrides.pipelineIdx = RendererVKLayout::EPipelineIndex::UnlitOpaque;
       overrides.excludeFromRayTracing = true; // debug markers: drawn, but never block shadow rays / GI / AO
       baseShapes.initialize(*sceneData, &overrides);
       // don't spawn sun sphere.. it blocks the GI probes...
       //sunLightNode = baseShapes.spawnNodeForIdx(baseShapes.getSpawnIdxForPath("Sphere"), Transform(sunDir * sunDistance, sunSize, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0))));
    }
    {
       // Procedural sky sphere: inward-facing, unlit, excluded from ray tracing so it never blocks
       // sun/light shadow rays, GI, or AO (rays that miss still shade with the analytic skyRadiance()).
       std::unique_ptr<ISceneData> sceneData = ISceneData::createProceduralLoader();
       sceneData->initialize("skysphere", false, false);
       ObjectContainer::MaterialOverrides overrides;
       overrides.pipelineIdx = RendererVKLayout::EPipelineIndex::Sky; // analytic sky + sun disc; ignores the material texture
       overrides.excludeFromRayTracing = true;
       overrides.useSceneTextures = true;
       skySphere.initialize(*sceneData, &overrides);
       spawnedNodes.push_back(skySphere.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(glm::vec3(0.0f), 1000.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))));
    }
    
    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& evt)
        {
            // spawn sponza at camera pos with T
			//if (evt.scancode == SDL_Scancode::SDL_SCANCODE_T && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
			//{
			//	const glm::vec3 camPos = cameraController.getPosition();
			//	const glm::vec3 camForward = cameraController.getDirection();
			//	const float spawnDistance = 10.0f;
			//	const glm::vec3 spawnPos = camPos + camForward * spawnDistance;
            //    glm::quat orientation = cameraController.getOrientation();
            //    const glm::vec3 camUp = cameraController.getUp();
            //    const glm::vec3 camRight = glm::normalize(glm::cross(cameraController.getDirection(), camUp));
            //    orientation = glm::angleAxis(glm::radians(180.0f), camRight) * orientation;
			//	spawnedNodes.push_back(container.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(spawnPos, 1.0f, glm::normalize(orientation))));
			//}
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_F5 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                renderer.reloadShaders();
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_P && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                renderer.toggleGiProbeDebug();          // P: show/hide GI probe debug cubes
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_O && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                renderer.cycleGiProbeDebugMode();        // O: cycle irradiance <-> cellSize/LOD color
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_L && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                renderer.setSunLight(-cameraController.getDirection(), glm::vec3(1.0f), 5.0f); // aim the sun along the camera forward
				//sunLightNode.getTransform() = Transform(-cameraController.getDirection() * sunDistance, sunSize, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_1 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.resize(0);
                spawnedLightGeom.resize(0);
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_2 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.push_back(PointLight{ cameraController.getPosition(), 25.0f, glm::abs(glm::sphericalRand(1.0f)), 50.0f });
                spawnedLightGeom.push_back(baseShapes.spawnNodeForIdx(baseShapes.getSpawnIdxForPath("Sphere"), Transform(cameraController.getPosition(), 0.1f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_3 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.push_back(PointLight{ cameraController.getPosition(), 15.0f + glm::linearRand(0.5f, 1.5f), glm::abs(glm::sphericalRand(1.0f)), 30.0f });
                spawnedLightGeom.push_back(baseShapes.spawnNodeForIdx(baseShapes.getSpawnIdxForPath("Sphere"), Transform(cameraController.getPosition(), 0.1f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_4 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                spawnedLights.push_back(SpotLight{ cameraController.getPosition(), 12.5f, glm::vec3(1.0f, 0.95f, 0.8f), 20.0f, cameraController.getDirection(), glm::radians(25.0f), 0.25f });
				glm::quat orientation = cameraController.getOrientation();
				const glm::vec3 camUp = cameraController.getUp();
				const glm::vec3 camRight = glm::normalize(glm::cross(cameraController.getDirection(), camUp));
				orientation = glm::angleAxis(glm::radians(90.0f), camRight) * orientation;
                spawnedLightGeom.push_back(baseShapes.spawnNodeForIdx(baseShapes.getSpawnIdxForPath("Cone"), Transform(cameraController.getPosition(), 0.1f, orientation)));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_5 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                const glm::vec3 dir = cameraController.getDirection();
                const glm::vec3 camUp = cameraController.getUp();
                // The quad's up-axis is the camera up; align its right-axis with the camera right so
                // the emission normal (cross(up, right)) points along the camera's forward direction.
                const glm::vec3 up = glm::normalize(camUp);
                const glm::vec3 ref = glm::abs(up.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 right0 = glm::normalize(glm::cross(up, ref));
                const glm::vec3 camRight = glm::normalize(glm::cross(dir, camUp));
                float rotation = atan2f(glm::dot(glm::cross(right0, camRight), up), glm::dot(right0, camRight));
				// if (rotation < 0.0f) rotation += glm::two_pi<float>(); Lets encode use shadow in negative rotation
                spawnedLights.push_back(AreaLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 1.0f, 1.0f), 10.0f, camUp, 1.0f, 1.0f, rotation });

                const glm::quat orientation = glm::angleAxis(glm::radians(-90.0f), camRight) * cameraController.getOrientation();
				spawnedLightGeom.push_back(baseShapes.spawnNodeForIdx(baseShapes.getSpawnIdxForPath("Plane"), Transform(cameraController.getPosition(), 0.5f, orientation)));
            }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_6 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            {
                const glm::vec3 dir = cameraController.getDirection();
                const glm::vec3 camUp = cameraController.getUp();
                // The quad's up-axis is the camera up; align its right-axis with the camera right so
                // the emission normal (cross(up, right)) points along the camera's forward direction.
                const glm::vec3 up = glm::normalize(camUp);
                const glm::vec3 ref = glm::abs(up.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 right0 = glm::normalize(glm::cross(up, ref));
                const glm::vec3 camRight = glm::normalize(glm::cross(dir, camUp));
                const float rotation = atan2f(glm::dot(glm::cross(right0, camRight), up), glm::dot(right0, camRight));
                spawnedLights.push_back(TubeLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 0.9f, 0.7f), 10.0f, camUp, 0.1f, 1.0f, rotation });
            }

            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_7 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                for (int i = 0; i < 100; ++i)
                {
                    int x = 0; int y = 0;
					glm::vec3 position = glm::vec3(x * 30.0f + glm::linearRand(-11.0f, 11.0f), glm::linearRand(0.0f, 7.0f), y * 20.0f + glm::linearRand(-5.0f, 4.5f));
                    spawnedLights.push_back(PointLight{ position,
                        glm::linearRand(0.5f, 2.0f), glm::abs(glm::sphericalRand(1.0f)), glm::linearRand(7.0f, 10.0f) });
                    spawnedLightGeom.push_back(baseShapes.spawnNodeForIdx(baseShapes.getSpawnIdxForPath("Sphere"), Transform(position, 0.05f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
                }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_8 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                for (int x = 0; x < spawnCountX; ++x)
                    for (int y = 0; y < spawnCountY; ++y)
                        for (int i = 0; i < 75; ++i)
                        {
							glm::vec3 position = glm::vec3(x * 30.0f + glm::linearRand(-11.0f, 11.0f), glm::linearRand(0.0f, 7.0f), y * 20.0f + glm::linearRand(-5.0f, 4.5f));
                            spawnedLights.push_back(PointLight{ position,
                                glm::linearRand(0.5f, 2.0f), glm::abs(glm::sphericalRand(1.0f)), glm::linearRand(7.0f, 10.0f) });
                            spawnedLightGeom.push_back(baseShapes.spawnNodeForIdx(baseShapes.getSpawnIdxForPath("Sphere"), Transform(position, 0.05f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
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
        ui.update(deltaSec);
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

        const Frustum& frustum = renderer.beginFrame(camera);
        for (RenderNode& node : spawnedNodes)
        {
            //if (frustum.sphereInFrustum(node.getWorldBounds()))
            {
                renderer.renderNode(node);
            }
        }

		for (auto& light : spawnedLights)
		{
			renderer.addLightInfo(light);
		}

        for (RenderNode& node : spawnedLightGeom)
        {
            if (frustum.sphereInFrustum(node.getWorldBounds()))
            {
                renderer.renderNode(node);
            }
        }
        //renderer.renderNode(sunLightNode); // This blocks the sun GI...
        //renderer.addFogVolume({ .pos = {0, 5.5f, 0}, .density = 0.025f, .halfExtents = {12, 6, 6}, .edgeSoftness = 0.4f, .albedo = {1,1,1}, .emissive = 0.00f });
        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    return 0;
}