import Core;
import Core.Allocator;
import Core.Window;
import Entity;
import Entity.FreeFlyCameraController;
import File.FileSystem;
import Input;

import RendererVK;
import RendererVK.ObjectContainer;
import RendererVK.RenderNode;

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

    RendererVK& renderer = Globals::rendererVK;
    renderer.initialize(window, true);

    ObjectContainer objectContainer;
    objectContainer.initialize("Models/ship_dark.glb", true);

    ObjectContainer objectContainer2;
    objectContainer2.initialize("Models/tower.glb", true);

    const uint32 numX = 50;
    const uint32 numY = 50;
    for (int x = 0; x < numX; ++x)
    {
        for (int y = 0; y < numY; ++y)
        {
            RenderNode node = objectContainer.createNewRootNode(glm::vec3(x * 5.0f, 0, y * 8.0f), 1.0f, glm::quat(1, 0, 0, 0));
            node.updateRenderTransform();

            RenderNode node2 = objectContainer2.createNewRootNode(glm::vec3(x * 5.0f, 10, y * 8.0f), 1.0f, glm::quat(1, 0, 0, 0));
            node2.updateRenderTransform();
        }
    }

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
            sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "FPS: %i mem: %.2fmb instances: %i meshtypes: %i materials: %i", 
                frameCount, (double)(g_heapAllocator.getUsedSize() + getAlignedAllocatedSize()) / 1024.0 / 1024.0, 
                renderer.getNumMeshInstances(), renderer.getNumMeshTypes(), renderer.getNumMaterials());
            window.setTitle(windowTitleBuf);
            frameCount = 0;
            timeAccum = 0.0;
        }
    }

    return 0;
}