module UI.SceneView;

import Core.imgui;

// ---- helpers ---------------------------------------------------------------

// Fallback label for an entity with no name set yet (keeps the row from being blank).
static const char* fallbackLabel(Entity* entity)
{
	if (hasComponent<RenderComponent>(entity)) return "Render Entity";
	return "Entity";
}

static const char* displayLabel(Entity* entity)
{
	return entity->name.empty() ? fallbackLabel(entity) : entity->name.c_str();
}

static bool isAlive(Entity* entity)
{
	for (Entity* e : Globals::entityRegistry.getAll())
		if (e == entity)
			return true;
	return false;
}

static bool containsCI(const char* haystack, const char* needle)
{
	const char* end = haystack + std::strlen(haystack);
	auto it = std::search(haystack, end, needle, needle + std::strlen(needle),
		[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
	return it != end;
}

// An entity passes the filter if it or any descendant matches by display label.
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

// ---- SceneView -------------------------------------------------------------

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
		ImGui::SetTooltip("Add a scene entity under World");

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
		// Any entity can be reparented; reparentEntity() guards cycles / invalid targets.
		if (dragged != parent)
		{
			m_hasPendingReparent    = true;
			m_pendingReparentChild  = dragged;
			m_pendingReparentTarget = parent;
		}
	}
	ImGui::EndDragDropTarget();
}

void SceneView::renderEntityNode(Entity* entity)
{
	const bool hasFilter = m_searchBuffer[0] != '\0';
	if (hasFilter && !matchesFilter(entity, m_searchBuffer))
		return;

	SceneComponent* sc = getComponent<SceneComponent>(entity);   // null for loose entities
	const bool isScene = sc != nullptr;

	ImGui::PushID(entity);

	const bool isSelected = (m_selected == entity);
	const bool isRenaming = (m_renamingEntity == entity);

	// Scene entities render as tree nodes that always show the expand triangle (even with no children
	// yet) so the triangle reads as "has a SceneComponent / can accept children". Loose entities have
	// no arrow, so they render as plain Selectables — that way their selection box sits at their own
	// indent (like their scene siblings) while their label lines up with the siblings' arrow column,
	// instead of being pushed a further arrow-width to the right.
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_AllowOverlap;
	if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (isScene && hasFilter) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

	// Dim disabled scene nodes (loose entities have no enabled flag).
	const bool dimmed = isScene && !sc->enabled;
	if (dimmed)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

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
		// NoPadWithHalfSpacing: Selectable otherwise extends its highlight half an ItemSpacing to the
		// left, which would misalign it from the scene entities' TreeNode highlight. Disabling it lines
		// the two selection boxes up exactly.
		ImGui::Selectable(displayLabel(entity), isSelected,
			ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_NoPadWithHalfSpacing);
	}

	if (dimmed)
		ImGui::PopStyleColor();

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && !isRenaming)
		m_selected = entity;

	if (isSelected && !isRenaming && ImGui::IsKeyPressed(ImGuiKey_F2))
		beginRename(entity);
	if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete))
		m_pendingDelete = entity;

	renderContextMenu(entity);
	dragSourceFor(entity);
	if (isScene)
		dropTargetReparentUnder(entity);   // only scene entities can receive children

	// Visibility toggle (scene entities only), right-aligned over the row.
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
				renderEntityNode(child);
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void SceneView::renderContextMenu(Entity* entity)
{
	if (!ImGui::BeginPopupContextItem("##sv_node_ctx"))
		return;

	m_selected = entity;

	if (hasComponent<SceneComponent>(entity))   // only scene entities can hold children
	{
		if (ImGui::MenuItem("Add Child"))
		{
			m_hasPendingCreate    = true;
			m_pendingCreateParent = entity;
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();
	}
	if (ImGui::MenuItem("Rename", "F2"))
	{
		ImGui::CloseCurrentPopup();
		beginRename(entity);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Delete", "Del"))
		m_pendingDelete = entity;

	ImGui::EndPopup();
}

void SceneView::renderWorld()
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_DefaultOpen;

	Entity* world = Globals::entityRegistry.getWorldRoot();

	ImGui::SetNextItemOpen(m_worldOpen, ImGuiCond_Always);
	m_worldOpen = ImGui::TreeNodeEx("World", flags);

	// World is a real SceneComponent entity, so dropping any entity (scene or loose) onto it nests it
	// directly underneath.
	dropTargetReparentUnder(world);

	if (ImGui::BeginPopupContextItem("##sv_world_ctx"))
	{
		if (ImGui::MenuItem("Add Entity"))
		{
			m_hasPendingCreate    = true;
			m_pendingCreateParent = nullptr; // createSceneEntity already lands it under World
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (m_worldOpen)
	{
		if (SceneComponent* sc = getComponent<SceneComponent>(world))
			for (const EntityPtr& child : sc->children)
				renderEntityNode(child);
		ImGui::TreePop();
	}
}

void SceneView::applyPendingMutations()
{
	if (m_hasPendingCreate)
	{
		const std::string name = "Entity " + std::to_string(++m_entityCounter);
		EntityPtr created = createSceneEntity(0, Transform(), name.c_str());
		if (m_pendingCreateParent)
			reparentEntity(created, m_pendingCreateParent);
		m_selected = created.get();
		beginRename(m_selected);
		m_hasPendingCreate    = false;
		m_pendingCreateParent = nullptr;
	}
	if (m_pendingDelete)
	{
		if (m_selected == m_pendingDelete)        m_selected = nullptr;
		if (m_renamingEntity == m_pendingDelete)  m_renamingEntity = nullptr;
		removeEntity(m_pendingDelete);
		m_pendingDelete = nullptr;
	}
	if (m_hasPendingReparent)
	{
		reparentEntity(m_pendingReparentChild, m_pendingReparentTarget);
		m_hasPendingReparent    = false;
		m_pendingReparentChild  = nullptr;
		m_pendingReparentTarget = nullptr;
	}
}

void SceneView::render()
{
	// Drop stale references to entities destroyed since last frame.
	if (m_selected && !isAlive(m_selected))             m_selected = nullptr;
	if (m_renamingEntity && !isAlive(m_renamingEntity)) m_renamingEntity = nullptr;

	renderToolbar();
	ImGui::Separator();

	ImGui::BeginChild("##sv_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

	renderWorld();

	// Loose entities with no parent sit at the top level alongside World.
	for (Entity* entity : Globals::entityRegistry.getAll())
		if (entity->parent == nullptr && !hasComponent<SceneComponent>(entity))
			renderEntityNode(entity);

	const float remainingH = ImGui::GetContentRegionAvail().y;
	if (remainingH > 0.0f)
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, remainingH));

	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& !ImGui::IsAnyItemHovered())
	{
		m_selected = nullptr;
	}

	// Dropping onto empty space also unparents to the top level.
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
		ImGui::EndDragDropTarget();
	}

	ImGui::EndChild();

	applyPendingMutations();
}
