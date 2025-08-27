import Core;
import Core.Allocator;
import Core.Window;
import Core.SDL;
import Core.Frustum;

import Entity;
import Entity.FreeFlyCameraController;
import File.FileSystem;
import File.SceneData;
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
    cameraController.initialize(input, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    RendererVK& renderer = Globals::rendererVK;
    renderer.initialize(window, true);

    ObjectContainer baseShapeContainer;
    const char* objectNames[] = { "Cube", "Capsule", "Cone", "Plane", "Ramp", "Sphere", "Wedge" };
    {
        SceneData sceneData;
        sceneData.initialize("Models/baseshapes.glb", false, false);
        baseShapeContainer.initialize(sceneData);
    }
    //RenderNode sphereNode = baseShapeContainer.spawnNodeForPath("ROOT/Sphere", Transform(glm::vec3(0.0f), 1.0f, glm::quat(1, 0, 0, 0)));

    ObjectContainer boatContainer;
    {
        SceneData sceneData;
        sceneData.initialize("Models/ship_dark.glb", false, false);
        //sceneData.initialize("Models/baseshapes.glb", false, false);
        //sceneData.initialize("Models/test1.glb", false, false);
        boatContainer.initialize(sceneData);
    }

    const uint32 numX = 50;
    const uint32 numY = 50;
    std::array<std::array<RenderNode, numY>, numX> renderNodes;
    //NodeSpawnIdx idx = boatContainer.getSpawnIdxForPath("ship_dark_8angles/cannon_right"); // NodeSpawnIdx_ROOT;
    NodeSpawnIdx idx = NodeSpawnIdx_ROOT;
    for (int x = 0; x < numX; ++x)
    {
        for (int y = 0; y < numY; ++y)
        {
           renderNodes[x][y] = boatContainer.spawnNodeForIdx(idx, Transform(glm::vec3(x * 5.0f, 0, y * 8.0f), 1.0f, glm::quat(1,0,0,0)));
        }
    }

    std::vector<RenderNode> spawnedNodes;

    KeyboardListener* pKeyboardListener = input.addKeyboardListener();
    pKeyboardListener->onKeyPressed = [&](const SDL_KeyboardEvent& evt) {
        if (evt.keysym.scancode == SDL_Scancode::SDL_SCANCODE_1 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            spawnedNodes.push_back(
                boatContainer.spawnRootNode(Transform(cameraController.getPosition(), 1.0f, glm::normalize(glm::quatLookAt(cameraController.getDirection(), cameraController.getUp()))))
            );
        }
    };

    auto startTime = std::chrono::high_resolution_clock::now();
    auto titleUpdateTime = std::chrono::high_resolution_clock::now();
    auto fpsUpdateTime = std::chrono::high_resolution_clock::now();
    double timeAccum = 0.0;
    uint32 frameCount = 0;
    uint32 fps = 0;

    char windowTitleBuf[256];
    while (running)
    {
        const auto now = std::chrono::high_resolution_clock::now();
        const double deltaSec = std::chrono::duration<double>(now - startTime).count();
        startTime = now;

        input.update(deltaSec);
        cameraController.update(deltaSec);

        const Frustum& frustum = renderer.beginFrame(cameraController.getCamera());
        for (int x = 0; x < numX; ++x)
        {
            for (int y = 0; y < numY; ++y)
            {
                Transform& transform = renderNodes[x][y].getTransform();
                transform.pos.y = glm::sin((float)timeAccum + x * 0.2f + y * 0.2f);

                if (frustum.sphereInFrustum(renderNodes[x][y].getWorldBounds()))
                {
                    renderer.renderNode(renderNodes[x][y]);
                }
            }
        }

        for (RenderNode& node : spawnedNodes)
        {
            if (frustum.sphereInFrustum(node.getWorldBounds()))
            {
                renderer.renderNode(node);
            }
        }

        renderer.present(cameraController.getCamera());
        frameCount++;
        timeAccum += deltaSec;

        if (std::chrono::duration<double>(now - fpsUpdateTime).count() >= 1.0)
        {
            fps = frameCount;
            frameCount = 0;
            fpsUpdateTime = now;
        }

        if (std::chrono::duration<double>(now - titleUpdateTime).count() > 0.016)
        {
            glm::vec3 pos = cameraController.getPosition();
            glm::vec3 dir = cameraController.getDirection();
            sprintf_s(windowTitleBuf, sizeof(windowTitleBuf), "FPS: %i mem: %.2fmb instances: %i meshtypes: %i materials: %i, pos: %.1f, %.1f, %.1f, dir: %.1f, %.1f, %.1f",
                fps, (double)(g_heapAllocator.getUsedSize() + getAlignedAllocatedSize()) / 1024.0 / 1024.0,
                renderer.getNumMeshInstances(), renderer.getNumMeshTypes(), renderer.getNumMaterials(), pos.x, pos.y, pos.z, dir.x, dir.y, dir.z);
            window.setTitle(windowTitleBuf);
            titleUpdateTime = now;
        }
    }
    input.removeKeyboardListener(pKeyboardListener);

    return 0;
}