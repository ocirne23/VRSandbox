import Core;
import Core.Window;
import Entity;
import Entity.FreeFlyCameraController;
import File.FileSystem;
import File.SceneData;
import Core.Allocator;
import Input;
import Core.SDL;

import RendererVK;
import RendererVK.Mesh;
import RendererVK.MeshInstance;

int main()
{
    FileSystem::initialize();

    Window window;
    window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(1920, 1080));

    Input input;
    input.initialize();

    bool running = true;
    SystemEventListener* systemEventListener = input.addSystemEventListener();
    systemEventListener->onQuit = [&]() { running = false; };

    FreeFlyCameraController cameraController;
    cameraController.initialize(input, glm::vec3(10.0f, 0.0f, -10.0f), glm::vec3(0.0f, 0.0f, 0.0f));

    RendererVK& renderer = VK::g_renderer;
    renderer.initialize(window, true);

    std::vector<Mesh> meshSet;
    {
        SceneData scene;
        scene.initialize("Models/baseshapes.fbx");
        std::vector<MeshData>& sceneMeshes = scene.getMeshes();
        meshSet.reserve(sceneMeshes.size());
        for (int i = 0; i < sceneMeshes.size(); ++i)
        {
            meshSet.emplace_back().initialize(sceneMeshes[i], i);
        }
    }
    renderer.updateMeshSet(meshSet);

    const uint32 numX = 8;
    const uint32 numY = 8;
    std::vector<MeshInstance> meshInstances(numX * numY);
    for (int x = 0; x < numX; ++x)
    {
        for (int y = 0; y < 8; ++y)
        {
            meshInstances[x * numX + y].transform.pos = glm::vec3(x * 3.0f, y * 3.0f, 0.0f);
            meshInstances[x * numX + y].transform.scale = 1.0f;
            meshSet[(x * numX + y) % meshSet.size()].addInstance(&meshInstances[x * numX + y]);
        }
    }
    
    KeyboardListener* keyboardListener = input.addKeyboardListener();
    keyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& e)
        {
            if (e.type == SDL_EVENT_KEY_DOWN && e.keysym.scancode == SDL_SCANCODE_1)
            {
                MeshInstance& meshInstance = meshInstances.emplace_back();
                meshInstance.transform.pos = cameraController.getPosition() + cameraController.getDirection();
                meshInstance.transform.scale = 0.3f;
                meshInstance.transform.quat = glm::quatLookAt(-cameraController.getDirection(), cameraController.getUp());
                meshSet[0].addInstance(&meshInstance);
            }
            else if (e.type == SDL_EVENT_KEY_DOWN && e.keysym.scancode == SDL_SCANCODE_2)
            {
                MeshInstance& meshInstance = meshInstances.emplace_back();
                meshInstance.transform.pos = cameraController.getPosition() + cameraController.getDirection();
                meshInstance.transform.scale = 0.3f;
                meshInstance.transform.quat = glm::quatLookAt(-cameraController.getDirection(), cameraController.getUp());
                meshSet[1].addInstance(&meshInstance);
            }
            else if (e.type == SDL_EVENT_KEY_DOWN && e.keysym.scancode == SDL_SCANCODE_3 && !meshInstances.empty())
            {
                MeshInstance& meshInstance = meshInstances.back();
                meshSet[meshInstance.getMeshIdx()].removeInstance(&meshInstance);
                meshInstances.pop_back();
            }
        };
    
    auto startTime = std::chrono::high_resolution_clock::now();
    double timeAccum = 0.0;
    uint32 frameCount = 0;

    char windowTitleBuf[256];
    while (running)
    {
        const auto now = std::chrono::high_resolution_clock::now();
        const double deltaSec = std::chrono::duration<double>(now - startTime).count();
        startTime = now;

        input.update(deltaSec);
        cameraController.update(deltaSec);

        renderer.update(deltaSec, cameraController.getViewMatrix(), meshInstances);
        renderer.render();
        frameCount++;

        timeAccum += deltaSec;
        if (timeAccum > 1.0f)
        {
            sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "FPS: %i Used Mem: %f mb", frameCount, (double)(g_heapAllocator.getUsedSize() + getAlignedAllocatedSize()) / 1024.0 / 1024.0);
            window.setTitle(windowTitleBuf);
            frameCount = 0;
            timeAccum = 0.0;
        }
    }

    return 0;
}