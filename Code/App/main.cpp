import Core;
import Core.Allocator;
import Core.Window;
import Core.SDL;
import Core.Frustum;
import Core.Time;
import Core.glm;

import Entity;
import Entity.FreeFlyCameraController;
import File.FileSystem;
import File.ISceneData;
import Input;
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
    cameraController.initialize(glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));

    Renderer& renderer = Globals::rendererVK;
    renderer.initialize(window, EValidation::ENABLED, EVSync::DISABLED);

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
    /*
    std::vector<PointLight> spawnedPointLights;
    std::vector<AreaLight> spawnedAreaLights;
    std::vector<SpotLight> spawnedSpotLights;*/

    ObjectContainer container;
    ObjectContainer container2;
    ObjectContainer container3;
    const int spawnCountX = 10;
    const int spawnCountY = 10;
    /*
    {
        std::unique_ptr<ISceneData> sceneData = ISceneData::createProceduralLoader();
        sceneData->initialize("terrain", true, true);
        container2.initialize(*sceneData);
        
        for (int x = 0; x < spawnCountX; ++x)
            for (int y = 0; y < spawnCountY; ++y)
                spawnedNodes.push_back(container2.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(glm::vec3(x * 30.0f, -5.0f, y * 20.0f), 100.0f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
    }
    */
    {
        std::unique_ptr<ISceneData> sceneData = ISceneData::createAssimpLoader();
        sceneData->initialize("Models/sponza.glb", true, true);
        container.initialize(*sceneData);
        for (int x = 0; x < spawnCountX; ++x)
            for (int y = 0; y < spawnCountY; ++y)
                spawnedNodes.push_back(container.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(glm::vec3(x * 30.0f, 0, y * 20.0f), 1.0f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
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

    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& evt)
        {
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_F5 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                renderer.reloadShaders();
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_1 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                spawnedLights.resize(0);
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_2 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
				spawnedLights.push_back(PointLight{ cameraController.getPosition(), 50.0f, glm::abs(glm::sphericalRand(1.0f)), 100.0f });
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_3 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                spawnedLights.push_back(PointLight{ cameraController.getPosition(), 15.0f + glm::linearRand(0.5f, 1.5f), glm::abs(glm::sphericalRand(1.0f)), 30.0f });
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_4 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                spawnedLights.push_back(SpotLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 0.95f, 0.8f), 40.0f, cameraController.getDirection(), glm::radians(25.0f), 0.25f });
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
                const float rotation = atan2f(glm::dot(glm::cross(right0, camRight), up), glm::dot(right0, camRight));
                spawnedLights.push_back(AreaLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 0.9f, 0.7f), 5.0f, camUp, 1.0f, 1.0f, rotation });
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
                spawnedLights.push_back(TubeLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 0.9f, 0.7f), 5.0f, camUp, 0.1f, 1.0f, rotation });
            }

            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_7 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                for (int i = 0; i < 100; ++i)
                {
                    int x = 0; int y = 0;
                    spawnedLights.push_back(PointLight{ glm::vec3(x * 30.0f + glm::linearRand(-11.0f, 11.0f), glm::linearRand(0.0f, 7.0f), y * 20.0f + glm::linearRand(-5.0f, 4.5f)),
                        glm::linearRand(0.5f, 2.0f), glm::abs(glm::sphericalRand(1.0f)), glm::linearRand(7.0f, 10.0f) });
                }
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_8 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
                for (int x = 0; x < spawnCountX; ++x)
                    for (int y = 0; y < spawnCountY; ++y)
                        for (int i = 0; i < 75; ++i)
                        {
                            spawnedLights.push_back(PointLight{ glm::vec3(x * 30.0f + glm::linearRand(-11.0f, 11.0f), glm::linearRand(0.0f, 7.0f), y * 20.0f + glm::linearRand(-5.0f, 4.5f)),
                                glm::linearRand(0.5f, 2.0f), glm::abs(glm::sphericalRand(1.0f)), glm::linearRand(7.0f, 10.0f) });
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

        const Frustum& frustum = renderer.beginFrame(cameraController.getCamera());
        for (RenderNode& node : spawnedNodes)
        {
            if (frustum.sphereInFrustum(node.getWorldBounds()))
            {
                renderer.renderNode(node);
            }
        }

		for (auto& light : spawnedLights)
		{
			renderer.addLightInfo(light);
		}

        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    return 0;
}