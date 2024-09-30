import Core;
import Core.Allocator;
import Core.Window;
import Entity;
import Entity.FreeFlyCameraController;
import File.FileSystem;
import File.SceneData;
import Input;

import RendererVK;
import RendererVK.ObjectContainer;
import RendererVK.ObjectSpawner;
import RendererVK.RenderNode;

int main()
{
    FileSystem::initialize();

    Window window;
    //window.initialize("Vulkan", glm::ivec2(5, 35), glm::ivec2(2560, 1440));
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

    ObjectContainer baseShapeContainer;
    const char* objectNames[] = { "Cube", "Capsule", "Cone", "Plane", "Ramp", "Sphere", "Wedge" };
    ObjectSpawner baseShapeSpawners[std::size(objectNames)];
    {
        SceneData sceneData;
        sceneData.initialize("Models/baseshapes.fbx", false, false);
        baseShapeContainer.initialize(sceneData);

        for (int i = 0; i < std::size(objectNames); ++i)
        {
            baseShapeSpawners[i].initialize(baseShapeContainer, sceneData.getRootNode().findChild({ objectNames[i] }));
        }
    }

    ObjectContainer boatContainer;
    ObjectSpawner boatSpawner;
    {
        SceneData sceneData;
        sceneData.initialize("Models/ship_dark.glb", true, true);
        boatContainer.initialize(sceneData);
        boatSpawner.initialize(boatContainer, sceneData.getRootNode());
    }

    const uint32 numX = 200;
    const uint32 numY = 200;
    std::vector<std::vector<RenderNode>> renderNodes;
    for (int x = 0; x < numX; ++x)
    {
        renderNodes.emplace_back();
        for (int y = 0; y < numY; ++y)
        {
            renderNodes[x].emplace_back(boatSpawner.spawn(glm::vec3(x * 5.0f, 0, y * 8.0f), 1.0f, glm::quat(1, 0, 0, 0)));
        }
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    auto titleUpdateTime = std::chrono::high_resolution_clock::now();
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
        for (int x = 0; x < numX; ++x)
        {
            for (int y = 0; y < numY; ++y)
            {
                Transform& transform = renderNodes[x][y].getTransform();
                transform.pos.y = glm::sin((float)timeAccum + x * 0.2f + y * 0.2f);
                renderNodes[x][y].updateTransform();
            }
        }

        renderer.update(deltaSec, cameraController);
        renderer.render();
        frameCount++;
        timeAccum += deltaSec;

        if (std::chrono::duration<double>(now - titleUpdateTime).count() > 1.0)
        {
            sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "FPS: %i mem: %.2fmb instances: %i meshtypes: %i materials: %i",
                frameCount, (double)(g_heapAllocator.getUsedSize() + getAlignedAllocatedSize()) / 1024.0 / 1024.0,
                renderer.getNumMeshInstances(), renderer.getNumMeshTypes(), renderer.getNumMaterials());
            window.setTitle(windowTitleBuf);
            frameCount = 0;
            titleUpdateTime = now;
        }
    }

    return 0;
}