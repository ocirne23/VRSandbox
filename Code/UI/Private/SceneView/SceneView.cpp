module UI.SceneView;

import Core.imgui;

static const char* fallbackLabel(Entity* entity)
{
	return "Entity";
}

static const char* displayLabel(Entity* entity)
{
	return entity->name.empty() ? fallbackLabel(entity) : entity->name.c_str();
}

static bool containsCI(const char* haystack, const char* needle)
{
	const char* end = haystack + std::strlen(haystack);
	auto it = std::search(haystack, end, needle, needle + std::strlen(needle),
		[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
	return it != end;
}

static bool matchesFilter(Entity* entity, const char* filter)
{
	if (containsCI(displayLabel(entity), filter))
		return true;
	if (SceneComponent* sc = getComponent<SceneComponent>(entity))
		for (const EntityPtr& child : sc->children)
			if (matchesFilter(child, filter))
				return true;
	return false;
}

void SceneView::beginRename(Entity* entity)
{
	m_renamingEntity  = entity;
	m_focusRenameNext = true;
	strncpy_s(m_renameBuffer, sizeof(m_renameBuffer), entity->name.c_str(), sizeof(m_renameBuffer) - 1);
	m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
}

void SceneView::renderToolbar()
{
	if (ImGui::Button("+ Add"))
	{
		m_hasPendingCreate    = true;
		m_pendingCreateParent = nullptr;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Add a scene entity");

	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##sv_search", "Search...", m_searchBuffer, sizeof(m_searchBuffer));
}

bool SceneView::dragSourceFor(Entity* entity)
{
	if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		return false;
	ImGui::SetDragDropPayload("SV_ENTITY", &entity, sizeof(Entity*));
	ImGui::Text("Move: %s", displayLabel(entity));
	ImGui::EndDragDropSource();
	return true;
}

void SceneView::dropTargetReparentUnder(Entity* parent)
{
	if (!ImGui::BeginDragDropTarget())
		return;
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SV_ENTITY"))
	{
		Entity* dragged = *static_cast<Entity**>(payload->Data);
		if (dragged != parent)
		{
			m_hasPendingReparent    = true;
			m_pendingReparentChild  = dragged;
			m_pendingReparentTarget = parent;
		}
	}
	acceptAssetSpawnPayload(parent);   // also accept asset-browser drops to spawn a child here
	ImGui::EndDragDropTarget();
}

void SceneView::acceptAssetSpawnPayload(Entity* parent)
{
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		m_changes.push_back({ EntityChange::CreateHierarchy{
			std::string(static_cast<const char*>(payload->Data)), EntityPtr(parent) } });
}

void SceneView::renderEntityNode(Entity* entity, bool ancestorLocked)
{
	const bool hasFilter = m_searchBuffer[0] != '\0';
	if (hasFilter && !matchesFilter(entity, m_searchBuffer))
		return;

	SceneComponent* sc = getComponent<SceneComponent>(entity);   // null for loose entities
	const bool isScene = sc != nullptr;
	const bool locked = ancestorLocked || entity->isPrefabInstance(); // part of a prefab instance

	ImGui::PushID(entity);

	const bool isSelected = (m_selected == entity);
	const bool isRenaming = (m_renamingEntity == entity);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_AllowOverlap;
	if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (isScene && hasFilter) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

	// Prefab instances show in blue (Unity-style); a disabled entity's grey takes precedence.
	const bool dimmed = isScene && !sc->enabled;
	int pushedColors = 0;
	if (dimmed)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		++pushedColors;
	}
	else if (locked)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.62f, 0.95f, 1.0f));
		++pushedColors;
	}
	// A locked prefab instance can't receive a dragged entity, so suppress its hover highlight while an
	// entity is being dragged — otherwise the highlight wrongly suggests it's a valid drop target.
	if (locked)
		if (const ImGuiPayload* dnd = ImGui::GetDragDropPayload(); dnd && dnd->IsDataType("SV_ENTITY"))
		{
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
			++pushedColors;
		}

	bool open = false;
	if (isRenaming)
	{
		if (isScene)
		{
			open = ImGui::TreeNodeEx("##rn", flags);
			ImGui::SameLine();
		}
		const float inputWidth = ImGui::GetContentRegionAvail().x
			- ImGui::GetTextLineHeight() - ImGui::GetStyle().ItemSpacing.x * 2.0f - 4.0f;
		ImGui::SetNextItemWidth(inputWidth);
		if (m_focusRenameNext)
		{
			ImGui::SetKeyboardFocusHere();
			m_focusRenameNext = false;
		}
		if (ImGui::InputText("##sv_rename", m_renameBuffer, sizeof(m_renameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
		{
			entity->name = m_renameBuffer;
			m_renamingEntity = nullptr;
		}
		if (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && !m_focusRenameNext)
			m_renamingEntity = nullptr;  // clicked away
	}
	else if (isScene)
	{
		open = ImGui::TreeNodeEx(displayLabel(entity), flags);
	}
	else
	{
		ImGui::Selectable(displayLabel(entity), isSelected,
			ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_NoPadWithHalfSpacing);
	}

	if (pushedColors)
		ImGui::PopStyleColor(pushedColors);

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && !isRenaming)
		m_selected = entity;

	if (isSelected && !isRenaming && ImGui::IsKeyPressed(ImGuiKey_F2) && !ancestorLocked)
		beginRename(entity);
	if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ancestorLocked)
		m_pendingDelete = entity;

	renderContextMenu(entity, locked);
	if (!ancestorLocked)
		dragSourceFor(entity);             // an internal prefab node can't be dragged out of its instance
	if (isScene && !locked)
		dropTargetReparentUnder(entity);   // a locked instance can't receive children

	if (isScene)
	{
		const float btnW = ImGui::GetTextLineHeight() + 4.0f;
		const float posX = ImGui::GetWindowWidth()
			- btnW - ImGui::GetStyle().ScrollbarSize - ImGui::GetStyle().WindowPadding.x;
		ImGui::SameLine(posX);
		ImGui::PushID("##vis");
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, sc->enabled
			? ImGui::GetStyleColorVec4(ImGuiCol_Text)
			: ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if (ImGui::SmallButton(sc->enabled ? "o" : "-"))
			sc->enabled = !sc->enabled;
		ImGui::PopStyleColor(2);
		ImGui::PopID();
	}

	if (open)
	{
		if (isScene)
			for (const EntityPtr& child : sc->children)
				renderEntityNode(child, locked);
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void SceneView::renderContextMenu(Entity* entity, bool locked)
{
	if (!ImGui::BeginPopupContextItem("##sv_node_ctx"))
		return;

	m_selected = entity;

	// An entity can be edited as a whole (renamed/deleted) unless it's an internal part of a locked
	// prefab instance — i.e. only when its parent isn't itself locked.
	const bool canEdit = (entity->parent == nullptr) || !entity->parent->isPrefabLocked();

	if (locked)
	{
		if (ImGui::MenuItem("Unpack Prefab"))   // break the link so the instance becomes editable
		{
			if (Entity* root = entity->nearestPrefabInstance())
				root->setPrefabInstance(false);
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();
	}

	if (hasComponent<SceneComponent>(entity) && !locked)   // a locked instance can't gain children
	{
		if (ImGui::MenuItem("Add Child"))
		{
			m_hasPendingCreate    = true;
			m_pendingCreateParent = entity;
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();
	}
	if (ImGui::MenuItem("Rename", "F2", false, canEdit))
	{
		ImGui::CloseCurrentPopup();
		beginRename(entity);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Delete", "Del", false, canEdit))
		m_pendingDelete = entity;

	ImGui::EndPopup();
}

void SceneView::applyPendingMutations()
{
	if (m_hasPendingCreate)
	{
		const std::string name = "Entity " + std::to_string(++m_entityCounter);
		m_changes.push_back({ EntityChange::AddSceneEntity{ name, EntityPtr(m_pendingCreateParent) } }); // new root → app takes ownership
		m_hasPendingCreate    = false;
		m_pendingCreateParent = nullptr;
	}
	if (m_pendingDelete)
	{
		if (m_selected == m_pendingDelete)        m_selected = nullptr;
		if (m_renamingEntity == m_pendingDelete)  m_renamingEntity = nullptr;
		m_changes.push_back({ EntityChange::Delete{ EntityPtr(m_pendingDelete) } }); // own it until polled, before detachFromOwner drops the scene-graph ref
		detachFromOwner(m_pendingDelete);
		m_pendingDelete = nullptr;
	}
	if (m_hasPendingReparent)
	{
		Entity* child     = m_pendingReparentChild;
		Entity* target    = m_pendingReparentTarget; // nullptr → top level (root)
		Entity* oldParent = child->parent;

		if (oldParent != target)
			m_changes.push_back({ EntityChange::Reparent{ EntityPtr(child), EntityPtr(target) } });

		child->reparentEntity(target);

		m_hasPendingReparent    = false;
		m_pendingReparentChild  = nullptr;
		m_pendingReparentTarget = nullptr;
	}
}

void SceneView::render(const std::vector<EntityPtr>& rootEntities)
{

	renderToolbar();
	ImGui::Separator();

	ImGui::BeginChild("##sv_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

	for (Entity* entity : rootEntities)
		if (entity->parent == nullptr)
			renderEntityNode(entity, false);

	const float remainingH = ImGui::GetContentRegionAvail().y;
	if (remainingH > 0.0f)
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, remainingH));

	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& !ImGui::IsAnyItemHovered())
	{
		m_selected = nullptr;
	}

	if (ImGui::BeginDragDropTargetCustom(
		ImRect(ImGui::GetWindowPos(),
			ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
				   ImGui::GetWindowPos().y + ImGui::GetWindowSize().y)),
		ImGui::GetID("##sv_root_drop")))
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SV_ENTITY"))
		{
			Entity* dragged = *static_cast<Entity**>(payload->Data);
			if (dragged->parent != nullptr)
			{
				m_hasPendingReparent    = true;
				m_pendingReparentChild  = dragged;
				m_pendingReparentTarget = nullptr;
			}
		}
		acceptAssetSpawnPayload(nullptr);   // asset dropped on empty space spawns at the World root
		ImGui::EndDragDropTarget();
	}

	ImGui::EndChild();

	applyPendingMutations();
}
