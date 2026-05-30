module UI.SceneView;

import Core.imgui;

// ---- helpers ---------------------------------------------------------------

static bool isAncestorOf(const SceneNode& ancestor, const SceneNode& node)
{
	const SceneNode* p = node.parent;
	while (p)
	{
		if (p == &ancestor) return true;
		p = p->parent;
	}
	return false;
}

static bool matchesFilter(const SceneNode& node, const char* filter)
{
	const std::string& name = node.name;
	auto it = std::search(name.begin(), name.end(), filter, filter + std::strlen(filter),
		[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
	if (it != name.end()) return true;
	for (const auto& child : node.children)
		if (matchesFilter(*child, filter)) return true;
	return false;
}

static std::unique_ptr<SceneNode> cloneSubtree(const SceneNode& src, SceneNode* newParent)
{
	auto clone       = std::make_unique<SceneNode>();
	clone->name      = src.name;
	clone->enabled   = src.enabled;
	clone->entityId  = src.entityId;
	clone->parent    = newParent;
	for (const auto& child : src.children)
		clone->children.push_back(cloneSubtree(*child, clone.get()));
	return clone;
}

// ---- SceneView – public API ------------------------------------------------

SceneNode* SceneView::addNode(SceneNode* parent, std::string name)
{
	auto node    = std::make_unique<SceneNode>();
	node->name   = std::move(name);
	node->parent = parent;
	SceneNode* ptr = node.get();
	containerOf(parent).push_back(std::move(node));
	return ptr;
}

void SceneView::removeNode(SceneNode* node)
{
	deleteNode(node);
}

SceneNode* SceneView::duplicateNode(SceneNode* src)
{
	if (!src) return nullptr;
	auto clone   = cloneSubtree(*src, src->parent);
	clone->name += " (Copy)";
	SceneNode* ptr = clone.get();

	auto& container = containerOf(src->parent);
	auto it = std::find_if(container.begin(), container.end(),
		[src](const auto& up) { return up.get() == src; });
	container.insert(it != container.end() ? std::next(it) : container.end(), std::move(clone));
	return ptr;
}

// ---- internals -------------------------------------------------------------

std::vector<std::unique_ptr<SceneNode>>& SceneView::containerOf(SceneNode* node)
{
	return node ? node->children : m_roots;
}

void SceneView::deleteNode(SceneNode* node)
{
	if (!node) return;
	if (m_selected == node || (m_selected && isAncestorOf(*node, *m_selected)))
		m_selected = nullptr;
	if (m_renamingNode == node)
		m_renamingNode = nullptr;

	auto& container = containerOf(node->parent);
	auto it = std::find_if(container.begin(), container.end(),
		[node](const auto& up) { return up.get() == node; });
	if (it != container.end())
		container.erase(it);
}

void SceneView::reparentNode(SceneNode* node, SceneNode* newParent)
{
	if (!node || node == newParent) return;
	if (newParent && isAncestorOf(*node, *newParent)) return;
	if (node->parent == newParent) return;

	auto& oldContainer = containerOf(node->parent);
	auto it = std::find_if(oldContainer.begin(), oldContainer.end(),
		[node](const auto& up) { return up.get() == node; });
	if (it == oldContainer.end()) return;

	auto nodeOwner  = std::move(*it);
	oldContainer.erase(it);
	nodeOwner->parent = newParent;
	containerOf(newParent).push_back(std::move(nodeOwner));
}

void SceneView::beginRename(SceneNode* node)
{
	m_renamingNode    = node;
	m_focusRenameNext = true;
	strncpy_s(m_renameBuffer, sizeof(m_renameBuffer), node->name.c_str(), sizeof(m_renameBuffer) - 1);
	m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
}

// ---- Toolbar ---------------------------------------------------------------

void SceneView::renderToolbar()
{
	if (ImGui::Button("+ Add"))
	{
		SceneNode* created = addNode(nullptr, "Entity " + std::to_string(++m_entityCounter));
		m_selected = created;
		beginRename(created);
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Add root entity");

	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##sv_search", "Search...", m_searchBuffer, sizeof(m_searchBuffer));
}

// ---- Node rendering --------------------------------------------------------

void SceneView::renderNode(SceneNode& node)
{
	const bool hasFilter = m_searchBuffer[0] != '\0';
	if (hasFilter && !matchesFilter(node, m_searchBuffer))
		return;

	const bool isLeaf      = node.children.empty();
	const bool isSelected  = (m_selected == &node);
	const bool isRenaming  = (m_renamingNode == &node);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_AllowOverlap;
	if (isLeaf)     flags |= ImGuiTreeNodeFlags_Leaf;
	if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (hasFilter)  ImGui::SetNextItemOpen(true, ImGuiCond_Always);

	// Dim disabled nodes
	if (!node.enabled)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

	bool open = false;
	if (isRenaming)
	{
		// Render the tree arrow with a blank label, then overlay an InputText
		open = ImGui::TreeNodeEx(("##rn_" + node.name).c_str(), flags);
		ImGui::SameLine();
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
			if (m_renameBuffer[0] != '\0')
				node.name = m_renameBuffer;
			m_renamingNode = nullptr;
		}
		if (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && !m_focusRenameNext)
			m_renamingNode = nullptr;  // cancelled (clicked away)
	}
	else
	{
		open = ImGui::TreeNodeEx(node.name.c_str(), flags);
	}

	if (!node.enabled)
		ImGui::PopStyleColor();

	// Selection on click (but not when toggling the arrow)
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen() && !isRenaming)
		m_selected = &node;

	// F2 starts rename for selected node
	if (isSelected && !isRenaming && ImGui::IsKeyPressed(ImGuiKey_F2))
		beginRename(&node);

	// Delete key removes selected node
	if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete))
		m_pendingDelete = &node;

	renderContextMenu(&node);

	// Drag source
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		SceneNode* ptr = &node;
		ImGui::SetDragDropPayload("SV_NODE", &ptr, sizeof(SceneNode*));
		ImGui::Text("Move: %s", node.name.c_str());
		ImGui::EndDragDropSource();
	}

	// Drop target – make this node the new parent
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SV_NODE"))
		{
			SceneNode* dragged = *static_cast<SceneNode**>(payload->Data);
			if (dragged != &node && !isAncestorOf(*dragged, node))
			{
				m_hasPendingReparent    = true;
				m_pendingReparentNode   = dragged;
				m_pendingReparentTarget = &node;
			}
		}
		ImGui::EndDragDropTarget();
	}

	// Visibility toggle – right-aligned, overlaps the tree row
	{
		const float btnW  = ImGui::GetTextLineHeight() + 4.0f;
		const float posX  = ImGui::GetWindowWidth()
			- btnW - ImGui::GetStyle().ScrollbarSize - ImGui::GetStyle().WindowPadding.x;
		ImGui::SameLine(posX);
		ImGui::PushID("##vis");
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, node.enabled
			? ImGui::GetStyleColorVec4(ImGuiCol_Text)
			: ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if (ImGui::SmallButton(node.enabled ? "o" : "-"))
			node.enabled = !node.enabled;
		ImGui::PopStyleColor(2);
		ImGui::PopID();
	}

	if (open)
	{
		for (auto& child : node.children)
			renderNode(*child);
		ImGui::TreePop();
	}
}

// ---- Context menu ----------------------------------------------------------

void SceneView::renderContextMenu(SceneNode* node)
{
	const char* popupId = node ? "##sv_node_ctx" : "##sv_empty_ctx";
	if (!ImGui::BeginPopupContextItem(popupId))
		return;

	if (node)
	{
		m_selected = node;

		if (ImGui::MenuItem("Add Child"))
		{
			SceneNode* child = addNode(node, "Entity " + std::to_string(++m_entityCounter));
			m_selected = child;
			ImGui::CloseCurrentPopup();
			beginRename(child);
		}
		if (ImGui::MenuItem("Duplicate"))
		{
			SceneNode* dup = duplicateNode(node);
			m_selected = dup;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Rename", "F2"))
		{
			ImGui::CloseCurrentPopup();
			beginRename(node);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Delete", "Del"))
			m_pendingDelete = node;
	}
	else
	{
		if (ImGui::MenuItem("Add Entity"))
		{
			SceneNode* created = addNode(nullptr, "Entity " + std::to_string(++m_entityCounter));
			m_selected = created;
			ImGui::CloseCurrentPopup();
			beginRename(created);
		}
	}

	ImGui::EndPopup();
}

// ---- render ----------------------------------------------------------------

void SceneView::render()
{
	renderToolbar();
	ImGui::Separator();

	ImGui::BeginChild("##sv_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

	for (auto& root : m_roots)
		renderNode(*root);

	// Empty-space interactions
	const float remainingH = ImGui::GetContentRegionAvail().y;
	if (remainingH > 0.0f)
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, remainingH));

	// Right-click on empty space
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)
		&& !ImGui::IsAnyItemHovered())
	{
		ImGui::OpenPopup("##sv_empty_ctx");
		m_selected = nullptr;
	}
	renderContextMenu(nullptr);

	// Click on empty space deselects
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& !ImGui::IsAnyItemHovered())
	{
		m_selected = nullptr;
	}

	// Drop on empty area → reparent to root
	if (ImGui::BeginDragDropTargetCustom(
		ImRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y)), 
		ImGui::GetID("##sv_root_drop")))
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SV_NODE"))
		{
			SceneNode* dragged = *static_cast<SceneNode**>(payload->Data);
			if (dragged->parent != nullptr)
			{
				m_hasPendingReparent    = true;
				m_pendingReparentNode   = dragged;
				m_pendingReparentTarget = nullptr;
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::EndChild();

	// ---- Apply deferred mutations ----
	if (m_pendingDelete)
	{
		deleteNode(m_pendingDelete);
		m_pendingDelete = nullptr;
	}
	if (m_hasPendingReparent)
	{
		reparentNode(m_pendingReparentNode, m_pendingReparentTarget);
		m_hasPendingReparent = false;
		m_pendingReparentNode = m_pendingReparentTarget = nullptr;
	}
}
