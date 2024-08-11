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
import RendererVK.ObjectContainer;
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

    ObjectContainer objectContainer;
    objectContainer.initialize("Models/ship_dark.gltf");
    objectContainer.updateInstancePositions();

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

        renderer.update(deltaSec, cameraController);
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