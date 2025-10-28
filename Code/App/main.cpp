import Core;
import Core.Allocator;
import Core.Window;
import Core.SDL;
import Core.Frustum;
import Core.Time;

import Entity;
import Entity.FreeFlyCameraController;
import File.FileSystem;
import File.SceneData;
import Input;
import UI;

import RendererVK;
import RendererVK.ObjectContainer;
import RendererVK.RenderNode;

int main()
{
    FileSystem::initialize();

    Window window;
    window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(1920, 1080));

    Input& input = Globals::input;
    input.initialize();

    FreeFlyCameraController cameraController;
    cameraController.initialize(glm::vec3(-20.0f, 20.0f, -20.0f), glm::vec3(50.0f, 0.0f, 50.0f));

    RendererVK& renderer = Globals::rendererVK;
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

    ObjectContainer boatContainer;
    {
        SceneData sceneData;
        //sceneData.initialize("Models/dragon.glb", true, true);
        //sceneData.initialize("Models/ship_dark.glb", true, true);
        sceneData.initialize("Models/sponza.glb", true, true);
        boatContainer.initialize(sceneData);
    }

   std::vector<RenderNode> spawnedNodes;
   for (int x = 0; x < 10; ++x)
       for (int y = 0; y < 10; ++y)
           spawnedNodes.push_back(boatContainer.spawnNodeForIdx(NodeSpawnIdx_ROOT, Transform(glm::vec3(x * 50.0f, 0, y * 30.0f), 1.0f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));

    KeyboardListener* pKeyboardListener = input.addKeyboardListener();
    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& evt) {
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_1 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            spawnedNodes.push_back(boatContainer.spawnRootNode(Transform(cameraController.getPosition(), 1.0f, glm::normalize(glm::quatLookAt(cameraController.getDirection(), cameraController.getUp())))));
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
            sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "FPS: %i mem: %.2fmb instances: %i meshtypes: %i materials: %i, pos: %.1f, %.1f, %.1f, dir: %.1f, %.1f, %.1f",
                fps, (double)(Globals::allocator.getUsedSize() + getAlignedAllocatedSize()) / 1024.0 / 1024.0,
                renderer.getNumMeshInstances(), renderer.getNumMeshTypes(), renderer.getNumMaterials(), pos.x, pos.y, pos.z, dir.x, dir.y, dir.z);
            window.setTitle(windowTitleBuf);
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
            //Transform& transform = node.getTransform();
            //transform.pos.y = glm::sin((float)Globals::time.getElapsedSec() + transform.pos.x * 0.1f + transform.pos.z * 0.1f);
            if (frustum.sphereInFrustum(node.getWorldBounds()))
            {
                renderer.renderNode(node);
            }
        }

        ui.render();
        renderer.present();
        frameCount++;
    }
    input.removeKeyboardListener(pKeyboardListener);
    input.removeSystemEventListener(pSystemEventListener);
    return 0;
}

/*
    ObjectContainer baseShapeContainer;
    const char* objectNames[] = { "Cube", "Capsule", "Cone", "Plane", "Ramp", "Sphere", "Wedge" };
    {
        SceneData sceneData;
        sceneData.initialize("Models/baseshapes.glb", false, false);
        baseShapeContainer.initialize(sceneData);
    }
    RenderNode sphereNode = baseShapeContainer.spawnNodeForPath("ROOT/Sphere", Transform(glm::vec3(0.0f), 1.0f, glm::quat(1, 0, 0, 0)));

        std::for_each(std::execution::par_unseq, std::begin(spawnedNodes), std::end(spawnedNodes), [&](RenderNode& node)
        {
            Transform& transform = node.getTransform();
            transform.pos.y = glm::sin((float)Globals::time.getElapsedSec() + transform.pos.x * 0.1f + transform.pos.z * 0.1f);
            if (frustum.sphereInFrustum(node.getWorldBounds()))
            {
                renderer.renderNodeThreadSafe(node);
            }
        });
*/