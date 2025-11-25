module UI;

import Core.imgui;

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

        static bool second_time = false;
        if (second_time)
        {
            second_time = false;
            //ImGui::SetWindowFocus("Viewport");
            ImGui::SetWindowFocus("Script");
        }

        static bool first_time = true;
        if (first_time)
        {
            first_time = false;
            second_time = true;

            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

            ImGuiID dock_id_right, dock_id_up, dock_id_left, dock_id_down;
            ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, &dock_id_left, &dock_id_right);
            ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Up, 0.8f, &dock_id_up, &dock_id_down);

            ImGui::DockBuilderDockWindow("Sidebar", dock_id_left);
            ImGui::DockBuilderDockWindow("Content", dock_id_down);
            ImGui::DockBuilderDockWindow("Script", dock_id_up);
            ImGui::DockBuilderDockWindow("Viewport", dock_id_up);
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
        ImGui::Button("Button 1");
        ImGui::Button("Button 2");
        ImGui::Button("Button 3");

        ImGui::End();
        ImGui::PopStyleVar(1);
    }

    {
        ImGui::Begin("Sidebar");
        ImGui::Text("m_isViewportGrabbed: %i", (int)m_isViewportGrabbed);
        ImGui::Text("m_isViewportFocused: %i", (int)m_isViewportFocused);
        ImGui::Text("m_hasViewportGainedFocus: %i", (int)m_hasViewportGainedFocus);
        ImGui::Text("m_viewportRect: %i,%i:%i,%i", m_viewportRect.min.x, m_viewportRect.min.y, m_viewportRect.max.x, m_viewportRect.max.y);
        ImGui::End();
    }

    {
        ImGui::Begin("Content");
        ImGui::Button("Button 1");
        ImGui::Button("Button 2");
        ImGui::Button("Button 3");
        ImGui::End();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));


        ImGui::Begin("Script");
        if (ImGui::IsWindowFocused())
            m_scene.update(deltaSec);
        ImGui::End();

        ImGui::PopStyleVar(1);
    }
}

void UI::render()
{
    ImGui::Render();
}