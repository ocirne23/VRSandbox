module UI.AssetBrowser;

import Core.imgui;
import Entity;
import Entity.Prefab;

// ---- helpers ---------------------------------------------------------------

static bool isImageFile(const std::filesystem::path& p)
{
	const auto ext = p.extension().string();
	return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
}

static bool isMeshFile(const std::filesystem::path& p)
{
	const auto ext = p.extension().string();
	return ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae";
}

static bool isShaderFile(const std::filesystem::path& p)
{
	const auto ext = p.extension().string();
	return ext == ".glsl" || ext == ".hlsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".spv";
}

static bool isSceneFile(const std::filesystem::path& p)
{
	const auto ext = p.extension().string();
	return ext == ".scene" || ext == ".prefab" || ext == ".pre";
}

// Asset description files parsed by the Scene library (.oc = ObjectContainer, .ent = Entity).
static bool isObjectFile(const std::filesystem::path& p)
{
	const auto ext = p.extension().string();
	return ext == ".oc" || ext == ".ent";
}

// Entities (.ent) and prefabs (.pre) can be dragged into the viewport to spawn; ObjectContainers
// (.oc) are dependency-only.
static bool isSpawnableFile(const std::filesystem::path& p)
{
	const auto ext = p.extension().string();
	return ext == ".ent" || ext == ".pre";
}

static const char* fileIcon(const std::filesystem::path& p)
{
	if (std::filesystem::is_directory(p)) return "[Dir]";
	if (isImageFile(p))                  return "[Img]";
	if (isMeshFile(p))                   return "[Msh]";
	if (isShaderFile(p))                 return "[Shd]";
	if (isSceneFile(p))                  return "[Scn]";
	if (isObjectFile(p))                 return "[Obj]";
	return "[Fil]";
}

static ImVec4 fileColor(const std::filesystem::path& p)
{
	if (std::filesystem::is_directory(p)) return ImVec4(1.0f, 0.85f, 0.4f, 1.0f);   // yellow
	if (isImageFile(p))                   return ImVec4(0.4f, 0.8f,  1.0f, 1.0f);   // cyan
	if (isMeshFile(p))                    return ImVec4(0.6f, 1.0f,  0.6f, 1.0f);   // green
	if (isShaderFile(p))                  return ImVec4(1.0f, 0.6f,  0.3f, 1.0f);   // orange
	if (isSceneFile(p))                   return ImVec4(0.9f, 0.5f,  1.0f, 1.0f);   // purple
	if (isObjectFile(p))                  return ImVec4(0.5f, 0.9f,  1.0f, 1.0f);   // light blue
	return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);                                        // grey
}

// Marks the just-submitted item as a drag source carrying an asset file path, so it can be
// dropped onto the Viewport to spawn. Payload "ASSET_FILE" is the null-terminated absolute path.
static void assetDragSource(const std::filesystem::path& p)
{
	if (!isSpawnableFile(p))
		return;
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		const std::string path = p.string();
		ImGui::SetDragDropPayload("ASSET_FILE", path.c_str(), path.size() + 1);
		ImGui::Text("Spawn %s", p.filename().string().c_str());
		ImGui::EndDragDropSource();
	}
}

static std::string truncateLabel(const std::string& name, float maxWidth)
{
	if (ImGui::CalcTextSize(name.c_str()).x <= maxWidth)
		return name;
	// Binary-search for the longest prefix that fits with "..."
	size_t lo = 0, hi = name.size();
	while (lo + 1 < hi)
	{
		const size_t mid = (lo + hi) / 2;
		if (ImGui::CalcTextSize((name.substr(0, mid) + "...").c_str()).x <= maxWidth)
			lo = mid;
		else
			hi = mid;
	}
	return name.substr(0, lo) + "...";
}

// ---- AssetBrowser ----------------------------------------------------------

void AssetBrowser::initialize()
{
	m_rootPath = std::filesystem::current_path();
	m_currentPath = m_rootPath;
}

void AssetBrowser::setRootPath(const std::filesystem::path& path)
{
	std::error_code ec;
	const auto canonical = std::filesystem::canonical(path, ec);
	if (!ec && std::filesystem::is_directory(canonical, ec))
	{
		m_rootPath    = canonical;
		m_currentPath = canonical;
		m_selectedPath.clear();
	}
}

void AssetBrowser::render()
{
	renderToolbar();
	ImGui::Separator();

	// Left pane – directory tree
	ImGui::BeginChild("##ab_left", ImVec2(m_leftPaneWidth, 0.0f), ImGuiChildFlags_Borders);
	renderDirectoryTree(m_rootPath);
	ImGui::EndChild();

	ImGui::SameLine();

	// Drag splitter
	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.28f, 0.28f, 0.28f, 0.60f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.50f, 0.50f, 0.80f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.50f, 0.50f, 1.00f));
	ImGui::Button("##ab_splitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;
		m_leftPaneWidth  = std::clamp(m_leftPaneWidth, 80.0f, 600.0f);
	}
	if (ImGui::IsItemHovered())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// Right pane – content grid or list
	ImGui::BeginChild("##ab_right", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
	if (m_listView)
		renderContentList();
	else
		renderContentGrid();
	acceptPrefabDrop();
	ImGui::EndChild();
}

// Dropping an entity dragged from the Scene hierarchy here saves it (and its children) as a ".pre"
// prefab in the current folder. The payload "SV_ENTITY" is an Entity* set by the Scene panel.
void AssetBrowser::acceptPrefabDrop()
{
	const ImVec2 mn = ImGui::GetWindowPos();
	const ImVec2 mx = ImVec2(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y);
	if (!ImGui::BeginDragDropTargetCustom(ImRect(mn, mx), ImGui::GetID("##ab_prefab_drop")))
		return;
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SV_ENTITY"))
	{
		Entity* entity = *static_cast<Entity**>(payload->Data);
		const std::string name = entity->name.empty() ? std::string("Prefab") : entity->name;
		const std::filesystem::path out = m_currentPath / (name + ".pre");
		savePrefab(entity, out.string());
		m_selectedPath = out;
	}
	ImGui::EndDragDropTarget();
}

// ---- Toolbar ---------------------------------------------------------------

void AssetBrowser::renderToolbar()
{
	// Up button
	const bool canGoUp = (m_currentPath != m_rootPath) && m_currentPath.has_parent_path();
	if (!canGoUp)
	{
		ImGui::BeginDisabled();
	}
	if (ImGui::SmallButton(" ^ ##ab_up"))
		navigateUp();
	if (!canGoUp)
	{
		ImGui::EndDisabled();
	}

	ImGui::SameLine();

	// Breadcrumb
	{
		std::vector<std::filesystem::path> parts;
		for (const auto& c : m_currentPath)
			parts.push_back(c);

		std::filesystem::path accumulated;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			accumulated /= parts[i];
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::SmallButton(parts[i].string().c_str()))
				navigateTo(accumulated);
			ImGui::PopID();
			if (i + 1 < parts.size())
			{
				ImGui::SameLine(0.0f, 2.0f);
				ImGui::TextDisabled("/");
				ImGui::SameLine(0.0f, 2.0f);
			}
		}
	}

	// Search bar + view toggle + icon size slider – right-aligned
	const float searchWidth  = 180.0f;
	const float sliderWidth  = 80.0f;
	const float toggleWidth  = 40.0f;
	const float rightWidth   = searchWidth + toggleWidth + (m_listView ? 0.0f : sliderWidth + 4.0f) + 12.0f;
	ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - rightWidth);
	ImGui::SetNextItemWidth(searchWidth);
	ImGui::InputTextWithHint("##ab_search", "Search...", m_searchBuf, sizeof(m_searchBuf));

	ImGui::SameLine();
	const bool listViewBeforeClick = m_listView;
	if (listViewBeforeClick)
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	if (ImGui::Button("List", ImVec2(toggleWidth, 0.0f)))
		m_listView = !m_listView;
	if (listViewBeforeClick)
		ImGui::PopStyleColor();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(m_listView ? "Switch to icon view" : "Switch to list view");

	if (!m_listView)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(sliderWidth);
		ImGui::SliderFloat("##ab_iconsize", &m_iconSize, 40.0f, 128.0f, "%.0f");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Icon size");
	}
}

// ---- Directory tree --------------------------------------------------------

void AssetBrowser::renderDirectoryTree(const std::filesystem::path& dir)
{
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec))
		return;

	const bool isCurrent = (dir == m_currentPath);

	bool hasSubDirs = false;
	for (const auto& e : std::filesystem::directory_iterator(dir, ec))
		if (e.is_directory()) { hasSubDirs = true; break; }

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (isCurrent)  flags |= ImGuiTreeNodeFlags_Selected;
	if (!hasSubDirs) flags |= ImGuiTreeNodeFlags_Leaf;
	if (dir == m_rootPath) flags |= ImGuiTreeNodeFlags_DefaultOpen;

	const std::string label = dir.filename().string().empty()
		? dir.string()
		: dir.filename().string();

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
	const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
	ImGui::PopStyleColor();

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		navigateTo(dir);

	if (open)
	{
		std::vector<std::filesystem::path> subDirs;
		for (const auto& e : std::filesystem::directory_iterator(dir, ec))
			if (e.is_directory()) subDirs.push_back(e.path());
		std::sort(subDirs.begin(), subDirs.end());
		for (const auto& sub : subDirs)
			renderDirectoryTree(sub);
		ImGui::TreePop();
	}
}

// ---- Content grid ----------------------------------------------------------

void AssetBrowser::renderContentGrid()
{
	const float cellSize    = m_iconSize + 20.0f;
	const float panelWidth  = ImGui::GetContentRegionAvail().x;
	const int   columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));
	const std::string filter(m_searchBuf);

	std::error_code ec;
	std::vector<std::filesystem::directory_entry> entries;
	for (const auto& e : std::filesystem::directory_iterator(m_currentPath, ec))
	{
		if (!filter.empty())
		{
			const std::string fname = e.path().filename().string();
			// Case-insensitive substring match
			auto it = std::search(fname.begin(), fname.end(), filter.begin(), filter.end(),
				[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
			if (it == fname.end()) continue;
		}
		entries.push_back(e);
	}

	std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
		const bool ad = a.is_directory(), bd = b.is_directory();
		if (ad != bd) return ad > bd;
		return a.path().filename() < b.path().filename();
	});

	if (ImGui::BeginTable("##ab_grid", columnCount, ImGuiTableFlags_None))
	{
		for (const auto& entry : entries)
		{
			ImGui::TableNextColumn();
			const std::filesystem::path& p = entry.path();
			const std::string name = p.filename().string();
			const bool isSelected  = (p == m_selectedPath);

			ImGui::PushID(name.c_str());

			// Highlight selected item
			if (isSelected)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			else
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

			// Colored icon text inside the button
			ImGui::PushStyleColor(ImGuiCol_Text, fileColor(p));
			const bool clicked = ImGui::Button(fileIcon(p), ImVec2(m_iconSize, m_iconSize));
			ImGui::PopStyleColor(2); // Text + Button

			assetDragSource(p);

			// Selection on single click
			if (clicked)
				m_selectedPath = p;

			// Navigate into directory on double-click
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (entry.is_directory())
					navigateTo(p);
			}

			renderContextMenu(p);

			// File name label (truncated to cell width)
			const std::string display = truncateLabel(name, cellSize - 4.0f);
			ImGui::TextUnformatted(display.c_str());
			if (display != name && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", name.c_str());

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// Click on empty space deselects
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& !ImGui::IsAnyItemHovered())
	{
		m_selectedPath.clear();
	}
}

// ---- Content list ----------------------------------------------------------

void AssetBrowser::renderContentList()
{
	const std::string filter(m_searchBuf);

	std::error_code ec;
	std::vector<std::filesystem::directory_entry> entries;
	for (const auto& e : std::filesystem::directory_iterator(m_currentPath, ec))
	{
		if (!filter.empty())
		{
			const std::string fname = e.path().filename().string();
			auto it = std::search(fname.begin(), fname.end(), filter.begin(), filter.end(),
				[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
			if (it == fname.end()) continue;
		}
		entries.push_back(e);
	}

	std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
		const bool ad = a.is_directory(), bd = b.is_directory();
		if (ad != bd) return ad > bd;
		return a.path().filename() < b.path().filename();
	});

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##ab_list", 3, tableFlags))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("Type",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Size",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();

		for (const auto& entry : entries)
		{
			const std::filesystem::path& p = entry.path();
			const std::string name         = p.filename().string();
			const bool isSelected          = (p == m_selectedPath);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::PushID(name.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, fileColor(p));
			if (ImGui::Selectable(name.c_str(), isSelected,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
				ImVec2(0.0f, 0.0f)))
			{
				m_selectedPath = p;
			}
			ImGui::PopStyleColor();

			assetDragSource(p);

			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				if (entry.is_directory()) navigateTo(p);

			renderContextMenu(p);

			// Type column
			ImGui::TableSetColumnIndex(1);
			if (entry.is_directory())
				ImGui::TextDisabled("Folder");
			else
				ImGui::TextDisabled("%s", p.extension().string().c_str());

			// Size column
			ImGui::TableSetColumnIndex(2);
			if (!entry.is_directory())
			{
				std::error_code sec;
				const auto bytes = entry.file_size(sec);
				if (!sec)
				{
					if (bytes < 1024)
						ImGui::TextDisabled("%llu B", bytes);
					else if (bytes < 1024 * 1024)
						ImGui::TextDisabled("%.1f KB", bytes / 1024.0);
					else
						ImGui::TextDisabled("%.1f MB", bytes / (1024.0 * 1024.0));
				}
			}

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// Click on empty space deselects
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& !ImGui::IsAnyItemHovered())
	{
		m_selectedPath.clear();
	}
}

// ---- Context menu ----------------------------------------------------------

void AssetBrowser::renderContextMenu(const std::filesystem::path& p)
{
	if (!ImGui::BeginPopupContextItem("##ab_ctx"))
		return;

	m_selectedPath = p;

	if (ImGui::MenuItem("Copy path"))
		ImGui::SetClipboardText(p.string().c_str());

	if (ImGui::MenuItem("Copy filename"))
		ImGui::SetClipboardText(p.filename().string().c_str());

	ImGui::Separator();

#if defined(_WIN32)
	if (ImGui::MenuItem("Show in Explorer"))
	{
		const std::string cmd = "explorer /select,\"" + p.string() + "\"";
		std::system(cmd.c_str());
	}
#endif

	if (std::filesystem::is_directory(p))
	{
		ImGui::Separator();
		if (ImGui::MenuItem("Open folder"))
			navigateTo(p);
		if (ImGui::MenuItem("Set as root"))
			setRootPath(p);
	}

	ImGui::EndPopup();
}

// ---- Navigation ------------------------------------------------------------

void AssetBrowser::navigateTo(const std::filesystem::path& path)
{
	std::error_code ec;
	if (std::filesystem::is_directory(path, ec))
	{
		m_currentPath  = std::filesystem::canonical(path, ec);
		m_selectedPath.clear();
		m_searchBuf[0] = '\0';
	}
}

void AssetBrowser::navigateUp()
{
	if (m_currentPath != m_rootPath && m_currentPath.has_parent_path())
		navigateTo(m_currentPath.parent_path());
}
