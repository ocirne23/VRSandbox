module UI;

import Core;
import Core.imgui;
import Core.Log;
import Core.glm;
import Core.SDL;
import Entity;

import :imgui_node_editor;
import :Scene;
import :AssetBrowser;
import :SceneView;
import :PropertiesPanel;
import :OutputLog;
import :TweakPanel;
import :TextEditor;
import :ScriptEditor;
import :DSL;
import :Transpiler;

namespace
{
	// M1 walking-skeleton test (see the DSL editor plan's Milestone 1): hardcodes
	//   function update(deltaSec) print("DSL walking skeleton: Update ran") end
	// directly as DSLSymbols (bypassing the not-yet-built editor/parser), transpiles it, and writes the result
	// to Assets/Scripts/DslWalkingSkeletonTest.scr. Point an entity's Script Path (Properties panel) at
	// "Scripts/DslWalkingSkeletonTest.scr" and run the app to see the log line on Update. Temporary -- removed
	// once M6 wires the real editor save/open + Compile & Run against an authored DSL document.
	std::string generateDslWalkingSkeletonTestScript()
	{
		DSLScriptFile scriptFile;

		auto addLine = [&](int scopeLevel) -> DSLCodeLine&
		{
			auto line = std::make_unique<DSLCodeLine>();
			line->scopeLevel = scopeLevel;
			DSLCodeLine& lineRef = *line;
			scriptFile.lines.push_back(std::move(line));
			return lineRef;
		};
		auto addSymbol = [](DSLCodeLine& line, DSLSymbol::SymbolType type, auto&& data) -> DSLSymbol&
		{
			auto symbol = std::make_unique<DSLSymbol>();
			symbol->type = type;
			symbol->data = std::forward<decltype(data)>(data);
			symbol->line = &line;
			DSLSymbol& symbolRef = *symbol;
			line.symbols.push_back(std::move(symbol));
			return symbolRef;
		};

		// function update(float deltaSec)
		DSLCodeLine& header = addLine(0);
		DSLSymbol& deltaSecType = addSymbol(header, DSLSymbol::SymbolType::TypeDeclaration, DSLSymbol::TypeDeclaration{ DSLType::Float });
		DSLSymbol& deltaSecParam = addSymbol(header, DSLSymbol::SymbolType::VariableDeclaration, DSLSymbol::VariableDeclaration{ "deltaSec", &deltaSecType });
		addSymbol(header, DSLSymbol::SymbolType::FunctionDeclaration, DSLSymbol::FunctionDeclaration{ "update", { &deltaSecParam }, DSLType::Void });

		// Builtin `print` -- per DSL.ixx's ownership model, a builtin declaration isn't owned by any line; a
		// local here stands in for the static builtin registry M3/M5 will introduce.
		DSLSymbol printBuiltin;
		printBuiltin.type = DSLSymbol::SymbolType::FunctionDeclaration;
		printBuiltin.data = DSLSymbol::FunctionDeclaration{ "print", {}, DSLType::Void };

		// print("DSL walking skeleton: Update ran")
		DSLCodeLine& body = addLine(1);
		DSLSymbol& literal = addSymbol(body, DSLSymbol::SymbolType::Constant, DSLSymbol::Constant{ DSLType::String, "DSL walking skeleton: Update ran" });
		addSymbol(body, DSLSymbol::SymbolType::FunctionCall, DSLSymbol::FunctionCall{ &printBuiltin, nullptr, { DSLSymbol::CallArgument{ nullptr, &literal } } });

		std::string code;
		for (const std::string& line : Transpiler::transpile(scriptFile.lines))
		{
			code += line;
			code += "\n";
		}

		const std::string path = "Scripts/DslWalkingSkeletonTest.scr";
		std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
		file.write(code.data(), code.size());
		return path;
	}
}

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

void UI::update(const std::vector<EntityPtr>& rootEntities, double deltaSec)
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
            ImGui::DockBuilderSplitNode(dock_id_left,  ImGuiDir_Up, 0.8f, &dock_id_left_top, &dock_id_left_bottom);
            ImGui::DockBuilderSplitNode(dock_id_left_top, ImGuiDir_Right, 0.5f, &dock_id_properties, &dock_id_scene);

            ImGui::DockBuilderDockWindow("Scene",      dock_id_scene);
            ImGui::DockBuilderDockWindow("Properties", dock_id_properties);
            ImGui::DockBuilderDockWindow("Stats",      dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Log",        dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Tweaks",     dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Content",       dock_id_down);
            ImGui::DockBuilderDockWindow("Entity Editor",  dock_id_properties);
            ImGui::DockBuilderDockWindow("Script",        dock_id_up);
            ImGui::DockBuilderDockWindow("Text Editor",   dock_id_up);
            ImGui::DockBuilderDockWindow("Script Editor", dock_id_up);
            ImGui::DockBuilderDockWindow("Viewport",   dock_id_up);
            ImGui::DockBuilderFinish(dockspace_id);
        }

        ImGui::End();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool viewportOpen = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoBackground);

        // Focus/grab tracking stays unconditional (not gated on viewportOpen below): if we skipped this
        // while the Viewport tab is in the background, m_isViewportFocused would stay stuck at whatever it
        // was on the last visible frame instead of dropping to false — e.g. camera-look input would keep
        // responding to mouse/keyboard after the user switched to another tab entirely.
        const ImGuiContext* ctx = ImGui::GetCurrentContext();
        const bool isViewportGrabbed = (ctx->MovingWindow == ctx->CurrentWindow);
        const bool wasViewportGrabbed = m_isViewportGrabbed && !isViewportGrabbed;
        m_isViewportGrabbed = isViewportGrabbed;
        const bool isViewportFocused = viewportOpen && ImGui::IsWindowFocused(ImGuiFocusedFlags_DockHierarchy) && !m_isViewportGrabbed && !wasViewportGrabbed;
        m_hasViewportGainedFocus = isViewportFocused && !m_isViewportFocused;
        m_isViewportFocused = isViewportFocused;

        if (viewportOpen)
        {
            const ImVec2 size = ImGui::GetContentRegionAvail();
            const ImVec2 viewportPos = ImGui::GetCursorScreenPos();
            m_viewportRect = Rect(glm::ivec2(viewportPos.x, viewportPos.y), glm::ivec2(viewportPos.x + size.x, viewportPos.y + size.y));

            const ImRect dropRect(viewportPos, ImVec2(viewportPos.x + size.x, viewportPos.y + size.y));
            if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("##viewport_drop")))
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
                {
                    const char* droppedPath = static_cast<const char*>(payload->Data);
                    const ImVec2 mouse = ImGui::GetMousePos();
                    m_viewportChanges.push_back({ EntityChange::CreateViewport{
                        glm::ivec2(int(mouse.x), int(mouse.y)), std::string(droppedPath) } });
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(1);
    }

    {
        if (ImGui::Begin("Script"))
        {
            if (ImGui::Button("New"))
                m_scene.newGraph();
            ImGui::SameLine();
            if (ImGui::Button("Save"))
                m_scene.save();
            ImGui::SameLine();
            if (ImGui::Button("Save As..."))
                ImGui::OpenPopup("Save Script As");
            ImGui::SameLine();
            if (ImGui::Button("Compile & Run"))
            {
                m_scene.save();
                m_scriptReloadRequests.push_back(m_scene.scriptPath());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s  (right-click canvas to add nodes)",
                std::filesystem::path(m_scene.scriptPath()).filename().string().c_str());

            if (ImGui::BeginPopup("Save Script As"))
            {
                static char nameBuf[128] = "MyScript";
                ImGui::TextUnformatted("File name (saved under Scripts/ as .scr):");
                ImGui::SetNextItemWidth(220.0f);
                const bool entered = ImGui::InputText("##saveas", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((ImGui::Button("Save##as") || entered) && nameBuf[0] != '\0')
                {
                    std::string name = nameBuf;
                    if (!name.ends_with(".scr")) name += ".scr";
                    m_scene.setScriptPath("Scripts/" + name);
                    m_scene.save();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (m_openUnsavedScriptPopup)
            {
                ImGui::OpenPopup("Unsaved Script Changes");
                m_openUnsavedScriptPopup = false;
            }
            if (ImGui::BeginPopupModal("Unsaved Script Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("'%s' has unsaved changes.",
                    std::filesystem::path(m_scene.scriptPath()).filename().string().c_str());
                ImGui::Text("Switch to '%s'?",
                    std::filesystem::path(m_pendingScriptOpen).filename().string().c_str());
                ImGui::Separator();
                const bool save = ImGui::Button("Save");
                ImGui::SameLine();
                const bool discard = ImGui::Button("Discard");
                ImGui::SameLine();
                const bool cancel = ImGui::Button("Cancel");
                if (save)
                    m_scene.save();
                if (save || discard)
                    m_scene.open(m_pendingScriptOpen);
                if (save || discard || cancel)
                {
                    m_pendingScriptOpen.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // Feed the Trigger Audio nodes the sound aliases of the entity that owns the open script (its
            // AudioComponent). Unknown while the selection doesn't own the open script, so a script opened
            // standalone (asset browser) keeps whatever alias pins its file declared.
            {
                std::vector<std::string> aliases;
                bool aliasesKnown = false;
                if (Entity* selected = m_sceneView.getSelected())
                    if (ScriptComponent* script = getComponent<ScriptComponent>(selected);
                        script && script->scriptModule && script->scriptModule->scriptPath == m_scene.scriptPath())
                    {
                        aliasesKnown = true;
                        if (AudioComponent* audio = getComponent<AudioComponent>(selected))
                            for (const AudioComponent::SoundDesc& sound : audio->getSounds())
                                aliases.push_back(sound.alias);
                    }
                m_scene.setAudioAliases(aliasesKnown ? &aliases : nullptr);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            // NoScrollWithMouse/NoScrollbar: otherwise this child can pick up its own scroll offset (e.g. once
            // its content size ever grows past its visible rect for a frame) and starts eating the mouse wheel
            // itself before the node editor's own scroll-to-zoom gets a chance to see it.
            ImGui::BeginChild("ScriptCanvas", ImVec2(0.0f, 0.0f), false,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
            m_scene.update(deltaSec);
            ImGui::EndChild();
            ImGui::PopStyleVar(1);
        }
        ImGui::End();
    }

    {
        if (ImGui::Begin("Text Editor"))
            m_textEditor.render();
        ImGui::End();
    }

    {
        if (ImGui::Begin("Script Editor"))
        {
            m_scriptEditor.render();

            ImGui::Separator();
            if (ImGui::Button("[DSL M1] Generate + Compile Walking-Skeleton Test"))
                m_scriptReloadRequests.push_back(generateDslWalkingSkeletonTestScript());
            ImGui::SameLine();
            ImGui::TextDisabled("writes Scripts/DslWalkingSkeletonTest.scr -- point an entity's Script Path at it to run");
        }
        ImGui::End();
    }

    {
        if (ImGui::Begin("Scene"))
            m_sceneView.render(rootEntities);
        ImGui::End();
    }

    // When the hierarchy selection changes to an entity carrying a script, make that script active in the
    // editor (guarded by the unsaved-changes prompt below if the current graph has pending edits).
    if (Entity* selected = m_sceneView.getSelected(); selected != m_scriptSelectionTracked)
    {
        m_scriptSelectionTracked = selected;
        if (selected)
            if (ScriptComponent* script = getComponent<ScriptComponent>(selected))
				if (script->scriptModule && script->scriptModule->scriptPath != m_scene.scriptPath())
					requestOpenScript(script->scriptModule->scriptPath);
    }

    {
        if (ImGui::Begin("Log"))
            m_outputLog.render();
        ImGui::End();
    }

    if (ImGui::Begin("Stats"))
    {
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
        ImGui::Separator();
        ImGui::Text("texStream resident: %.1f MiB (%.1f%% of %.1f MiB budget)", m_renderStats.textureResidentBytes * toMiB,
            m_renderStats.textureBudgetBytes > 0 ? (float)m_renderStats.textureResidentBytes / m_renderStats.textureBudgetBytes * 100.0f : 0.0f,
            m_renderStats.textureBudgetBytes * toMiB);
        ImGui::Text("texStream desired: %.1f MiB, tail: %.1f MiB, pinned: %.1f MiB", m_renderStats.textureDesiredBytes * toMiB,
            m_renderStats.textureTailBytes * toMiB, m_renderStats.texturePinnedBytes * toMiB);
        ImGui::Text("texStream streamable: %u, opsInFlight: %u", m_renderStats.numStreamableTextures, m_renderStats.numStreamOpsInFlight);
        ImGui::Text("meshStream resident: %.1f MiB (of %.1f MiB streamable, %.1f MiB budget)", m_renderStats.meshResidentBytes * toMiB,
            m_renderStats.meshStreamableBytes * toMiB, m_renderStats.meshBudgetBytes * toMiB);
        ImGui::Text("meshStream cold: %.1f MiB, sets: %u, evicted: %u", m_renderStats.meshColdBytes * toMiB,
            m_renderStats.numMeshSets, m_renderStats.numEvictedMeshSets);
        ImGui::Text("static BLAS: %.1f MiB (compaction saved %.1f MiB)", m_renderStats.blasBytes * toMiB,
            m_renderStats.blasCompactionSavedBytes * toMiB);
        ImGui::Text("meshLOD groups: %u, picks L0-L4: %u/%u/%u/%u/%u", m_renderStats.numMeshLodGroups,
            m_renderStats.lodInstanceCounts[0], m_renderStats.lodInstanceCounts[1], m_renderStats.lodInstanceCounts[2],
            m_renderStats.lodInstanceCounts[3], m_renderStats.lodInstanceCounts[4]);
    }
    ImGui::End();

    {
        if (ImGui::Begin("Content"))
            m_assetBrowser.render();
        ImGui::End();

        // Route .scr file actions from the asset browser into the Script editor.
        if (std::string openPath = m_assetBrowser.takeScriptOpenRequest(); !openPath.empty())
            m_scene.open(openPath);
        if (std::string createPath = m_assetBrowser.takeScriptCreateRequest(); !createPath.empty())
        {
            m_scene.newGraph();
            m_scene.setScriptPath(createPath);
            m_scene.save();
        }

        // Route the asset browser's "Edit Entity" action into the Entity Editor.
        if (std::string editPath = m_assetBrowser.takeEntityEditRequest(); !editPath.empty())
            m_entityEditor.requestOpen(editPath);

        // Route the asset browser's "Open Text File" action into the Text Editor.
        if (std::string textPath = m_assetBrowser.takeTextOpenRequest(); !textPath.empty())
            m_textEditor.requestOpen(textPath);
    }

    {
        if (ImGui::Begin("Tweaks"))
            m_tweakPanel.render();
        ImGui::End();
    }

    {
        if (ImGui::Begin("Entity Editor"))
        {
            m_entityEditor.render(m_sceneView.getSelected());

            if (ImGui::BeginDragDropTargetCustom(
                ImRect(ImGui::GetWindowPos(),
                    ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                           ImGui::GetWindowPos().y + ImGui::GetWindowSize().y)),
                ImGui::GetID("##ee_drop")))
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
                    m_entityEditor.requestOpen(static_cast<const char*>(payload->Data));
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();

        // Route the entity editor's "Select Prefab" action into the asset browser.
        if (std::string revealPath = m_entityEditor.takeRevealRequest(); !revealPath.empty())
            m_assetBrowser.selectFile(revealPath);
    }

    {
        if (ImGui::Begin("Properties"))
            m_propertiesPanel.render(m_sceneView.getSelected());
        ImGui::End();
    }
}

void UI::copyScriptSelection()
{
    if (ImGui::GetIO().WantTextInput)
        return; // a text field elsewhere is active — let it handle its own copy instead of hijacking Ctrl+C
    m_scene.requestCopy();
}

void UI::pasteScriptSelection()
{
    if (ImGui::GetIO().WantTextInput)
        return; // ditto for paste
    m_scene.requestPaste();
}

void UI::handleKeyEvent(SDL_Event evt)
{
    m_scriptEditor.handleKeyEvent(evt);
}

void UI::requestOpenScript(const std::string& path)
{
    if (path.empty() || path == m_scene.scriptPath())
        return; // already the active script

    if (m_scene.isDirty())
    {
        m_pendingScriptOpen = path;       // defer the switch behind the unsaved-changes prompt
        m_openUnsavedScriptPopup = true;
    }
    else
    {
        m_scene.open(path);
    }
}

void UI::render()
{
    ImGui::Render();
}