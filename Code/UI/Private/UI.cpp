module UI;

import Core.imgui;
import Core.Log;
import Core.glm;

UI::~UI()
{
    if (m_nodeEditorContext != nullptr)
    {
        ed::DestroyEditor(m_nodeEditorContext);
        m_nodeEditorContext = nullptr;
    }
}

void UI::initialize()
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    assert(context != nullptr && "Imgui must be initialized by renderer first");
    (void)context;

    ImGui::GetStyle().Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0, 0.0, 0.0, 0.0);

    m_scene.initialize();
    m_assetBrowser.initialize();

    Log::info("UI initialized");
    Log::verbose("Demo scene hierarchy seeded");

    // Seed demo scene hierarchy
    SceneNode* world  = m_sceneView.addNode(nullptr, "World");
    SceneNode* lights = m_sceneView.addNode(world,   "Lights");
                        m_sceneView.addNode(lights,  "DirectionalLight");
                        m_sceneView.addNode(lights,  "PointLight");
    SceneNode* geo    = m_sceneView.addNode(world,   "Geometry");
                        m_sceneView.addNode(geo,     "StaticMesh_0");
                        m_sceneView.addNode(geo,     "StaticMesh_1");
                        m_sceneView.addNode(nullptr, "Camera");
}

struct LinkInfo
{
    ed::LinkId Id;
    ed::PinId  InputId;
    ed::PinId  OutputId;
};
void ImGuiEx_BeginColumn()
{
    ImGui::BeginGroup();
}

void ImGuiEx_NextColumn()
{
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
}

void ImGuiEx_EndColumn()
{
    ImGui::EndGroup();
}
bool                 m_FirstFrame = true;    // Flag set for first frame only, some action need to be executed once.
ImVector<LinkInfo>   m_Links;                // List of live links. It is dynamic unless you want to create read-only view over nodes.
int                  m_NextLinkId = 100;

void UI::update(double deltaSec)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        const ImGuiWindowFlags rootWindowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
        ImGui::Begin("Root", nullptr, rootWindowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspace_id = ImGui::GetID("Root");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), 0);

        static bool first_time = true;
        if (first_time)
        {
            first_time = false;

            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

            ImGuiID dock_id_right, dock_id_up, dock_id_left, dock_id_down;
            ImGuiID dock_id_left_top, dock_id_left_bottom;
            ImGuiID dock_id_scene, dock_id_properties;

            ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.3f, &dock_id_left, &dock_id_right);
            ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Up, 0.8f, &dock_id_up, &dock_id_down);
            ImGui::DockBuilderSplitNode(dock_id_left,  ImGuiDir_Up, 0.5f, &dock_id_left_top, &dock_id_left_bottom);
            ImGui::DockBuilderSplitNode(dock_id_left_top, ImGuiDir_Right, 0.5f, &dock_id_properties, &dock_id_scene);

            ImGui::DockBuilderDockWindow("Scene",      dock_id_scene);
            ImGui::DockBuilderDockWindow("Properties", dock_id_properties);
            ImGui::DockBuilderDockWindow("Stats",      dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Log",        dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Tweaks",     dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Content",    dock_id_down);
            ImGui::DockBuilderDockWindow("Script",     dock_id_up);
            ImGui::DockBuilderDockWindow("Viewport",   dock_id_up);
            ImGui::DockBuilderFinish(dockspace_id);
        }

        ImGui::End();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoBackground);

        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 viewportPos = ImGui::GetCursorScreenPos();
        m_viewportRect = Rect(glm::ivec2(viewportPos.x, viewportPos.y), glm::ivec2(viewportPos.x + size.x, viewportPos.y + size.y));

        const ImGuiContext* ctx = ImGui::GetCurrentContext();
        const bool isViewportGrabbed = (ctx->MovingWindow == ctx->CurrentWindow);
        const bool wasViewportGrabbed = m_isViewportGrabbed && !isViewportGrabbed;
        m_isViewportGrabbed = isViewportGrabbed;
        const bool isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_DockHierarchy) && !m_isViewportGrabbed && !wasViewportGrabbed;
        m_hasViewportGainedFocus = isViewportFocused && !m_isViewportFocused;
        m_isViewportFocused = isViewportFocused;

        // Accept assets dragged from the browser. Custom target (no item) so it never steals mouse
        // input from the camera controls when nothing is being dragged.
        const ImRect dropRect(viewportPos, ImVec2(viewportPos.x + size.x, viewportPos.y + size.y));
        if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("##viewport_drop")))
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
            {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                const ImVec2 mouse = ImGui::GetMousePos();
                m_assetDrops.push_back({ std::string(droppedPath), glm::vec2(mouse.x, mouse.y) });
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::End();
        ImGui::PopStyleVar(1);
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("Script");
        if (ImGui::IsWindowFocused())
            m_scene.update(deltaSec);
        ImGui::End();

        ImGui::PopStyleVar(1);
    }

    {
        ImGui::Begin("Scene");
        m_sceneView.render();
        ImGui::End();
    }

    {
        ImGui::Begin("Properties");
        m_propertiesPanel.render(m_sceneView.getSelected());
        ImGui::End();
    }

    {
        ImGui::Begin("Log");
        m_outputLog.render();
        ImGui::End();
    }

    {
        ImGui::Begin("Stats");
        ImGui::Text("numMeshInstances: %i (%.1f%%)", m_renderStats.numMeshInstances, (float)m_renderStats.numMeshInstances / m_renderStats.maxMeshInstances * 100.0f);
        ImGui::Text("numInstanceOffsets: %i (%.1f%%)", m_renderStats.numInstanceOffsets, (float)m_renderStats.numInstanceOffsets / m_renderStats.maxInstanceOffsets * 100.0f);
        ImGui::Text("numMeshTypes: %i (%.1f%%)", m_renderStats.numMeshTypes, (float)m_renderStats.numMeshTypes / m_renderStats.maxMeshTypes * 100.0f);
        ImGui::Text("numMaterials: %i (%.1f%%)", m_renderStats.numMaterials, (float)m_renderStats.numMaterials / m_renderStats.maxMaterials * 100.0f);
        ImGui::Text("numRenderNodes: %i (%.1f%%)", m_renderStats.numRenderNodes, (float)m_renderStats.numRenderNodes / m_renderStats.maxRenderNodes * 100.0f);
		ImGui::Text("numTextures: %i (%.1f%%)", m_renderStats.numTextures, (float)m_renderStats.numTextures / m_renderStats.maxTextures * 100.0f);
        ImGui::Text("numObjectContainers: %i", m_renderStats.numObjectContainers);
        ImGui::Text("numLightGrids: %i (%.1f%%)", m_renderStats.numLightGrids, (float)m_renderStats.numLightGrids / m_renderStats.maxLightGrids * 100.0f);
        ImGui::Text("lightGridMemUsageBytes: %llu (%.1f%%)", m_renderStats.lightGridMemUsageBytes, (float)m_renderStats.lightGridMemUsageBytes / m_renderStats.maxLightGridMemUsageBytes * 100.0f);
		ImGui::Text("vertexDataUsedBytes: %llu (%.1f%%)", m_renderStats.vertexDataUsedBytes, (float)m_renderStats.vertexDataUsedBytes / m_renderStats.maxVertexDataBytes * 100.0f);
		ImGui::Text("indexDataUsedBytes: %llu (%.1f%%)", m_renderStats.indexDataUsedBytes, (float)m_renderStats.indexDataUsedBytes / m_renderStats.maxIndexDataBytes * 100.0f);
        ImGui::Separator();
        const float toMiB = 1.0f / (1024.0f * 1024.0f);
        if (m_renderStats.gpuMemoryBudgetBytes > 0)
            ImGui::Text("gpuMemoryUsed: %.1f MiB (%.1f%% of %.1f MiB budget)", m_renderStats.gpuMemoryUsedBytes * toMiB,
                (float)m_renderStats.gpuMemoryUsedBytes / m_renderStats.gpuMemoryBudgetBytes * 100.0f, m_renderStats.gpuMemoryBudgetBytes * toMiB);
        else
            ImGui::Text("gpuMemoryUsed: %.1f MiB", m_renderStats.gpuMemoryUsedBytes * toMiB);
        ImGui::Text("gpuMemoryReserved: %.1f MiB", m_renderStats.gpuMemoryReservedBytes * toMiB);
        ImGui::End();
    }



    {
        ImGui::Begin("Content");
        m_assetBrowser.render();
        ImGui::End();
    }

    {
        ImGui::Begin("Tweaks");
        m_tweakPanel.render();
        ImGui::End();
    }
}

void UI::render()
{
    ImGui::Render();
}