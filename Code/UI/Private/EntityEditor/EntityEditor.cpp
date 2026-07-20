module UI;

import Core;
import Core.imgui;
import Core.glm;
import Entity;
import Physics;
import File;
import :EntityEditor;

// Copies a std::string into a fixed InputText buffer each frame and writes edits back immediately (same
// pattern PropertiesPanel uses for displayName) — the caller decides when to actually commit/respawn via
// ImGui::IsItemDeactivatedAfterEdit() right after this call.
static void inputTextStd(const char* label, std::string& value)
{
	char buf[256];
	strncpy_s(buf, sizeof(buf), value.c_str(), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	if (ImGui::InputText(label, buf, sizeof(buf)))
		value = buf;
}

static bool containsCI(const std::string& haystack, const char* needle)
{
	if (needle[0] == '\0')
		return true;
	std::string h = haystack, n = needle;
	for (char& c : h) c = (char)std::tolower((unsigned char)c);
	for (char& c : n) c = (char)std::tolower((unsigned char)c);
	return h.find(n) != std::string::npos;
}

template <typename Map>
static bool namePickerButton(const char* popupId, std::string& value, const Map& items, char* searchBuf, size_t searchBufSize)
{
	bool changed = false;
	if (ImGui::Button("Pick..."))
	{
		ImGui::OpenPopup(popupId);
		searchBuf[0] = '\0';
	}
	if (ImGui::BeginPopup(popupId))
	{
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##search", "Search...", searchBuf, searchBufSize);
		ImGui::Separator();
		ImGui::BeginChild("##list", ImVec2(220.0f, 200.0f));
		for (const auto& kv : items)
		{
			const std::string& name = kv.first;
			if (!containsCI(name, searchBuf))
				continue;
			if (ImGui::Selectable(name.c_str()))
			{
				value = name;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	return changed;
}

// Same idea as namePickerButton, for a plain list of names rather than a name->desc map (e.g. a single
// container's node paths, scoped down instead of every spawnable registered engine-wide).
static bool nodePickerButton(const char* popupId, std::string& value, const std::vector<std::string>& items, char* searchBuf, size_t searchBufSize)
{
	bool changed = false;
	if (ImGui::Button("Pick Node..."))
	{
		ImGui::OpenPopup(popupId);
		searchBuf[0] = '\0';
	}
	if (ImGui::BeginPopup(popupId))
	{
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##search", "Search...", searchBuf, searchBufSize);
		ImGui::Separator();
		ImGui::BeginChild("##list", ImVec2(220.0f, 200.0f));
		for (const std::string& name : items)
		{
			if (!containsCI(name, searchBuf))
				continue;
			if (ImGui::Selectable(name.c_str()))
			{
				value = name;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	return changed;
}

static std::string joinComma(const std::vector<std::string>& v)
{
	std::string out;
	for (size_t i = 0; i < v.size(); ++i)
	{
		if (i) out += ", ";
		out += v[i];
	}
	return out;
}

static std::vector<std::string> splitComma(const std::string& s)
{
	std::vector<std::string> out;
	size_t start = 0;
	while (start <= s.size())
	{
		size_t comma = s.find(',', start);
		size_t end = (comma == std::string::npos) ? s.size() : comma;
		size_t a = start, b = end;
		while (a < b && std::isspace((unsigned char)s[a])) ++a;
		while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
		if (b > a)
			out.push_back(s.substr(a, b - a));
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}
	return out;
}

// All .scr files under Assets/ (the working directory), as forward-slash paths relative to it — the
// same form ScriptComponent::scriptPath uses. Assets/Local holds generated script build output, not sources.
static void gatherScriptFiles(std::vector<std::string>& out)
{
	out.clear();
	std::error_code ec;
	const std::filesystem::path root = std::filesystem::current_path(ec);
	for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
	{
		if (it->is_directory(ec))
		{
			if (it->path().filename() == "Local")
				it.disable_recursion_pending();
			continue;
		}
		if (it->path().extension() != ".scr")
			continue;
		out.push_back(std::filesystem::relative(it->path(), root, ec).generic_string());
	}
	std::sort(out.begin(), out.end());
}

// Reads the root transform stored in a .pre file, so the editor's transform draft starts from what the
// file says rather than wherever the live entity happens to sit in the world.
static bool readPrefabFileTransform(const std::string& path, Transform& out)
{
	AssetNode doc;
	std::string error;
	if (path.empty() || !loadAssetFile(path, doc, error))
		return false;
	const AssetNode* prefab = doc.find("Prefab");
	if (!prefab)
		return false;
	if (const AssetNode* n = prefab->find("Position"))
		out.pos = n->asVec3(out.pos);
	if (const AssetNode* n = prefab->find("Rotation"))
		out.quat = glm::quat(glm::radians(n->asVec3(glm::degrees(glm::eulerAngles(out.quat)))));
	if (const AssetNode* n = prefab->find("Scale"))
		out.scale = n->asFloat(0, out.scale);
	return true;
}

std::string EntityEditor::currentId() const
{
	if (!m_path.empty())
		return std::filesystem::path(m_path).stem().string();
	return (m_editRoot && m_editRoot->hasName()) ? std::string(m_editRoot->getName()) : std::string("NewEntity");
}

// Serializes the document. With Sync off, the selected node's live transform (which the gizmo/world may
// have moved) is swapped for the editor's detached draft, so the file keeps what the Transform floats show.
std::string EntityEditor::serializeDocText() const
{
	if (!m_editRoot)
		return {};
	Entity* sel = m_selected.get();
	if (m_syncTransform || !sel)
		return serializePrefabText(m_editRoot.get(), currentId());

	const glm::vec3 livePos = sel->pos;
	const float     liveScale = sel->scale;
	const glm::quat liveRot = sel->rot;
	sel->pos = m_transformDraft.pos;
	sel->scale = m_transformDraft.scale;
	sel->rot = m_transformDraft.quat;
	std::string text = serializePrefabText(m_editRoot.get(), currentId());
	sel->pos = livePos;
	sel->scale = liveScale;
	sel->rot = liveRot;
	return text;
}

void EntityEditor::rebaseline()
{
	m_baselineText = serializeDocText();
}

bool EntityEditor::isDirty() const
{
	if (!m_editRoot)
		return false;
	return serializeDocText() != m_baselineText;
}

void EntityEditor::refreshDraftsFromEntity()
{
	Entity* e = m_selected.get();
	if (!e)
		return;

	m_hasScene = hasComponent<SceneComponent>(e);
	m_transformDraft = Transform(e->pos, e->scale, e->rot);
	if (e == m_editRoot.get())
		readPrefabFileTransform(m_path, m_transformDraft); // start from what the file stores, not the world pose

	m_hasRender = hasComponent<RenderComponent>(e);
	m_renderDraft = m_hasRender ? *getRenderSpawnInfo(e) : RenderComponent::SpawnInfo{};

	m_hasAnimator = hasComponent<AnimatorComponent>(e);
	m_animatorDraft = m_hasAnimator ? *getAnimatorSpawnInfo(e) : AnimatorComponent::SpawnInfo{};

	m_hasPhysics = hasComponent<PhysicsComponent>(e);
	m_physicsDraft = m_hasPhysics ? *getPhysicsSpawnInfo(e) : PhysicsComponent::SpawnInfo{};
	strncpy_s(m_physCollidesWithBuf, sizeof(m_physCollidesWithBuf), joinComma(m_physicsDraft.collidesWith).c_str(), sizeof(m_physCollidesWithBuf) - 1);

	m_hasAudio = hasComponent<AudioComponent>(e);
	m_audioDraft = m_hasAudio ? *getAudioSpawnInfo(e) : AudioComponent::SpawnInfo{};

	m_hasParticle = hasComponent<ParticleComponent>(e);
	m_particleDraft = m_hasParticle && getParticleSpawnInfo(e) ? *getParticleSpawnInfo(e) : ParticleComponent::SpawnInfo{};
	strncpy_s(m_particleEffectBuf, sizeof(m_particleEffectBuf), m_particleDraft.effectPath.c_str(), sizeof(m_particleEffectBuf) - 1);

	m_hasForce = hasComponent<ForceComponent>(e);
	m_forceDraft = m_hasForce && getForceSpawnInfo(e) ? *getForceSpawnInfo(e) : ForceComponent::SpawnInfo{};

	m_hasScript = hasComponent<ScriptComponent>(e);
	m_scriptDraft = m_hasScript && getScriptSpawnInfo(e) ? *getScriptSpawnInfo(e) : ScriptComponent::SpawnInfo{};
	if (m_hasScript)
		if (ScriptComponent* sc = getComponent<ScriptComponent>(e))
			m_scriptDraft.enabled = sc->enabled;
}

void EntityEditor::onOpened(EntityPtr root, const std::string& path)
{
	m_editRoot = root;
	m_selected = root;
	m_path = path;
	if (root)
		root->setFrozen(true); // freeze scripts/physics/animation while the document is open

	std::string suggested = path;
	if (suggested.empty() && root)
		suggested = "Entities/" + (root->hasName() ? std::string(root->getName()) : std::string("NewEntity")) + ".pre";
	strncpy_s(m_pathBuf, sizeof(m_pathBuf), suggested.c_str(), sizeof(m_pathBuf) - 1);
	m_pathBuf[sizeof(m_pathBuf) - 1] = '\0';

	refreshDraftsFromEntity();
	rebaseline();
}

void EntityEditor::onRespawned(EntityPtr oldEntity, EntityPtr newEntity)
{
	// Either or both may match: the respawned node could be the document root, the currently selected
	// node, or (commonly) both at once.
	if (m_editRoot.get() == oldEntity.get())
		m_editRoot = newEntity;
	if (m_selected.get() == oldEntity.get())
		m_selected = newEntity;
	// Deliberately NOT calling refreshDraftsFromEntity() here: the draft state (m_hasX/m_xDraft) is what
	// drove this respawn and stays authoritative. A component whose builder legitimately returned null
	// (e.g. Render with no mesh picked yet, Audio with no clips yet) should keep showing what the user
	// typed so far rather than silently reverting — it just won't be part of the entity until valid.
}

void EntityEditor::selectEntity(EntityPtr entity)
{
	m_selected = entity;
	refreshDraftsFromEntity();
}

void EntityEditor::addChild()
{
	if (!m_selected || !m_hasScene)
		return;
	const std::string name = m_newChildNameBuf[0] ? m_newChildNameBuf : "Entity";
	m_changes.push_back({ EntityChange::AddSceneEntity{ name, m_selected } });
}

void EntityEditor::removeChild(Entity* child)
{
	if (!child)
		return;
	EntityPtr keepAlive(child); // own it until this frame's changes are drained, before detachFromOwner drops the scene-graph ref
	detachFromOwner(child);
	if (m_selected.get() == child)
		selectEntity(m_editRoot);
	m_changes.push_back({ EntityChange::Delete{ keepAlive } });
}

void EntityEditor::closeCurrent()
{
	if (m_editRoot)
	{
		// Respawned children carry their own copy of the flag (World::handleEntityChange sets it at create),
		// so thaw the whole tree, not just the root.
		auto clearFrozen = [](auto&& self, Entity* e) -> void
		{
			e->setFrozen(false);
			if (SceneComponent* sc = getComponent<SceneComponent>(e))
				for (const EntityPtr& child : sc->children)
					self(self, child.get());
		};
		clearFrozen(clearFrozen, m_editRoot.get());
		if (m_ownsEntity)
			m_changes.push_back({ EntityChange::Delete{ m_editRoot } }); // a dedicated entity this editor spawned
		else if (m_wasPacked)
			m_editRoot->setPrefabInstance(true); // re-lock a scene entity we borrowed and unpacked to edit
	}
	m_editRoot = EntityPtr();
	m_selected = EntityPtr();
	m_path.clear();
	m_baselineText.clear();
	m_hasScene = m_hasRender = m_hasAnimator = m_hasPhysics = m_hasAudio = m_hasParticle = m_hasForce = m_hasScript = false;
	m_ownsEntity = true;
	m_wasPacked = false;
}

void EntityEditor::doSwitchOpen(const std::string& path)
{
	closeCurrent();
	m_changes.push_back({ EntityChange::OpenPrefabForEdit{ path } });
}

void EntityEditor::doSwitchNew(const std::string& name)
{
	closeCurrent();
	m_changes.push_back({ EntityChange::NewPrefab{ name } });
}

void EntityEditor::doSwitchOpenSelected(EntityPtr entity)
{
	if (!entity)
		return;
	closeCurrent();

	m_ownsEntity = false;
	m_wasPacked = entity->isPrefabInstance();
	entity->setPrefabInstance(false); // unpack: edit it freely, same as opening a prefab by path
	entity->setFrozen(true);    // freeze scripts/physics/animation while the document is open

	m_editRoot = entity;
	m_selected = entity;

	m_path.clear();
	if (const std::string* path = Globals::assetRegistry.findPrefab(entity->getPrefabName()))
		m_path = *path;
	const std::string suggested = m_path.empty() ? ("Entities/" + entity->getPrefabName() + ".pre") : m_path;
	strncpy_s(m_pathBuf, sizeof(m_pathBuf), suggested.c_str(), sizeof(m_pathBuf) - 1);
	m_pathBuf[sizeof(m_pathBuf) - 1] = '\0';

	refreshDraftsFromEntity();
	rebaseline();
}

void EntityEditor::doClose()
{
	closeCurrent();
}

void EntityEditor::requestOpen(const std::string& path)
{
	if (path.empty())
		return;
	if (m_editRoot && isDirty())
	{
		m_pendingSwitch     = PendingSwitch::OpenPath;
		m_pendingSwitchPath = path;
		m_openUnsavedPopup  = true;
		return;
	}
	doSwitchOpen(path);
}

void EntityEditor::requestNew(const std::string& name)
{
	if (m_editRoot && isDirty())
	{
		m_pendingSwitch      = PendingSwitch::New;
		m_pendingSwitchName  = name;
		m_openUnsavedPopup   = true;
		return;
	}
	doSwitchNew(name);
}

void EntityEditor::requestOpenSelected(Entity* entity)
{
	if (!entity || !entity->isPrefabInstance() || entity == m_editRoot.get())
		return;
	if (m_editRoot && isDirty())
	{
		m_pendingSwitch       = PendingSwitch::OpenSelected;
		m_pendingSwitchEntity = EntityPtr(entity);
		m_openUnsavedPopup    = true;
		return;
	}
	doSwitchOpenSelected(EntityPtr(entity));
}

void EntityEditor::requestClose()
{
	if (!m_editRoot)
		return;
	if (isDirty())
	{
		m_pendingSwitch    = PendingSwitch::Close;
		m_openUnsavedPopup = true;
		return;
	}
	doClose();
}

void EntityEditor::queueSave(const std::string& path)
{
	m_changes.push_back({ EntityChange::SavePrefab{ m_editRoot, path, serializeDocText() } });
	m_path = path;
	rebaseline();

	// A root respawned before its first save carries an empty sourceFile — respawn it once more so its
	// template (and the Properties "Asset" field) picks up the file it now lives in.
	if (m_editRoot && m_editRoot->getSourceFile() != m_path)
	{
		const Transform keepDraft = m_transformDraft; // selectEntity refreshes the draft from the live entity
		EntityPtr prevSelected = m_selected;
		selectEntity(m_editRoot);
		commitRespawn();
		if (prevSelected && prevSelected.get() != m_editRoot.get())
			selectEntity(prevSelected);
		m_transformDraft = keepDraft;
	}
}

void EntityEditor::trySave(const std::string& path)
{
	if (!m_editRoot || path.empty())
		return;

	std::error_code ec;
	if (path != m_path && std::filesystem::exists(path, ec))
	{
		m_pendingSavePath    = path;
		m_openOverwritePopup = true;
		return;
	}
	queueSave(path);
}

void EntityEditor::renderOverwritePopup()
{
	if (m_openOverwritePopup)
	{
		ImGui::OpenPopup("Overwrite Entity File?");
		m_openOverwritePopup = false;
	}
	if (ImGui::BeginPopupModal("Overwrite Entity File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("'%s' already exists. Overwrite it?", m_pendingSavePath.c_str());
		ImGui::Separator();
		if (ImGui::Button("Overwrite"))
		{
			queueSave(m_pendingSavePath);
			m_pendingSavePath.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_pendingSavePath.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void EntityEditor::renderUnsavedPopup()
{
	if (m_openUnsavedPopup)
	{
		ImGui::OpenPopup("Unsaved Entity Changes");
		m_openUnsavedPopup = false;
	}
	if (ImGui::BeginPopupModal("Unsaved Entity Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("The current entity has unsaved changes.");
		switch (m_pendingSwitch)
		{
		case PendingSwitch::New:          ImGui::TextUnformatted("Start a new entity anyway?"); break;
		case PendingSwitch::OpenSelected: ImGui::TextUnformatted("Switch to the selected entity anyway?"); break;
		case PendingSwitch::Close:        ImGui::TextUnformatted("Close anyway?"); break;
		default:                          ImGui::Text("Switch to '%s'?", m_pendingSwitchPath.c_str()); break;
		}
		ImGui::Separator();
		const bool save    = ImGui::Button("Save");
		ImGui::SameLine();
		const bool discard = ImGui::Button("Discard");
		ImGui::SameLine();
		const bool cancel  = ImGui::Button("Cancel");

		if (save)
			trySave(m_path.empty() ? m_pathBuf : m_path);
		if (save || discard)
		{
			switch (m_pendingSwitch)
			{
			case PendingSwitch::New:          doSwitchNew(m_pendingSwitchName); break;
			case PendingSwitch::OpenPath:      doSwitchOpen(m_pendingSwitchPath); break;
			case PendingSwitch::OpenSelected:  doSwitchOpenSelected(m_pendingSwitchEntity); break;
			case PendingSwitch::Close:         doClose(); break;
			default: break;
			}
		}
		if (save || discard || cancel)
		{
			m_pendingSwitch = PendingSwitch::None;
			m_pendingSwitchPath.clear();
			m_pendingSwitchName.clear();
			m_pendingSwitchEntity = EntityPtr();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void EntityEditor::renderToolbar()
{
	const bool canOpenSelected = m_sceneSelection && m_sceneSelection->isPrefabInstance();
	ImGui::BeginDisabled(!canOpenSelected);
	if (ImGui::Button("Open Selected"))
		requestOpenSelected(m_sceneSelection);
	ImGui::EndDisabled();
	if (!canOpenSelected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Select a (non-unpacked) prefab instance in the Scene panel first");
	ImGui::SameLine();

	ImGui::BeginDisabled(m_path.empty());
	if (ImGui::Button("Select Prefab"))
		m_revealRequest = m_path;
	ImGui::EndDisabled();
	if (m_path.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("No .pre file yet — save first");
	ImGui::SameLine();

	ImGui::BeginDisabled(!m_editRoot);
	if (ImGui::Button("Save"))
		trySave(m_pathBuf);
	ImGui::SameLine();
	if (ImGui::Button("Close"))
		requestClose();
	ImGui::EndDisabled();

	if (m_editRoot)
	{
		ImGui::SameLine();
		ImGui::TextDisabled(isDirty() ? "* unsaved changes" : "saved");
	}
}

void EntityEditor::renderTreeNode(Entity* node)
{
	ImGui::PushID(node);

	SceneComponent* sc = getComponent<SceneComponent>(node);
	const bool isLocked = node->isPrefabInstance(); // a "Prefab <name>" reference — edit the source file instead
	const bool hasChildren = !isLocked && sc && !sc->children.empty();
	const bool isRoot = (node->parent == nullptr);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	if (m_selected.get() == node) flags |= ImGuiTreeNodeFlags_Selected;

	const std::string label = node->hasName() ? std::string(node->getName()) : std::string("Entity");

	if (isLocked)
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.62f, 0.95f, 1.0f));
	const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
	if (isLocked)
		ImGui::PopStyleColor();

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		selectEntity(EntityPtr(node));

	// Drop an existing prefab here to add it as a locked reference child — a locked node can't itself
	// receive children (matches reparentEntity()'s own rule).
	if (!isLocked && sc)
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
				m_changes.push_back({ EntityChange::CreateHierarchy{
					std::string(static_cast<const char*>(payload->Data)), EntityPtr(node) } });
			ImGui::EndDragDropTarget();
		}

	if (ImGui::BeginPopupContextItem("##tree_ctx"))
	{
		if (isLocked && ImGui::MenuItem("Unpack Prefab")) // break the link so it becomes an editable inline entity
			node->setPrefabInstance(false);
		ImGui::EndPopup();
	}

	if (!isRoot)
	{
		ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
		if (ImGui::SmallButton("x"))
			m_pendingRemoveChild = node;
	}

	if (open && hasChildren)
	{
		for (EntityPtr& child : sc->children)
			renderTreeNode(child.get());
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void EntityEditor::renderTree()
{
	ImGui::SeparatorText("Hierarchy");
	ImGui::BeginChild("##ee_tree", ImVec2(0.0f, m_treeHeight), ImGuiChildFlags_Borders);
	if (m_editRoot)
		renderTreeNode(m_editRoot.get());
	ImGui::EndChild();

	// Drag handle: a thin invisible strip just below the tree that resizes it vertically.
	ImGui::InvisibleButton("##ee_tree_resize", ImVec2(-1.0f, 6.0f));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	if (ImGui::IsItemActive())
		m_treeHeight = std::clamp(m_treeHeight + ImGui::GetIO().MouseDelta.y, 60.0f, 600.0f);

	if (m_pendingRemoveChild)
	{
		removeChild(m_pendingRemoveChild);
		m_pendingRemoveChild = nullptr;
	}
}

void EntityEditor::renderNameAndTransform()
{
	strncpy_s(m_nameBuf, sizeof(m_nameBuf), m_selected->getName(), sizeof(m_nameBuf) - 1);
	m_nameBuf[sizeof(m_nameBuf) - 1] = '\0';
	ImGui::SetNextItemWidth(240.0f);
	if (ImGui::InputText("Name", m_nameBuf, sizeof(m_nameBuf)))
		m_selected->setName(m_nameBuf);

	bool enabled = m_selected->isEnabled();
	if (ImGui::Checkbox("Enabled", &enabled))
		m_selected->setEnabled(enabled); // direct live mutation — no respawn needed, matches PropertiesPanel

	if (ImGui::CollapsingHeader("Transform"))
	{
		ImGui::Checkbox("Sync", &m_syncTransform);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("On: mirror and edit the live entity's transform.\nOff: these values save to the file as-is, independent of where the entity sits in the world");
		if (m_syncTransform)
			m_transformDraft = Transform(m_selected->pos, m_selected->scale, m_selected->rot);

		bool edited = ImGui::DragFloat3("Position", &m_transformDraft.pos.x, 0.05f);
		edited |= ImGui::DragFloat("Scale", &m_transformDraft.scale, 0.01f, 0.0001f, 10000.0f);

		glm::vec3 euler = glm::degrees(glm::eulerAngles(m_transformDraft.quat));
		if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
		{
			m_transformDraft.quat = glm::quat(glm::radians(euler));
			edited = true;
		}

		if (edited && m_syncTransform)
		{
			m_selected->pos = m_transformDraft.pos;
			m_selected->scale = m_transformDraft.scale;
			m_selected->rot = m_transformDraft.quat;
		}
	}
}

void EntityEditor::renderSceneSection()
{
	if (!m_hasScene)
	{
		if (ImGui::Button("+ Add Scene"))
		{
			m_hasScene = true;
			commitRespawn();
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("scene");

	SceneComponent* sc = getComponent<SceneComponent>(m_selected.get());

	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputTextWithHint("##newchildname", "Child name", m_newChildNameBuf, sizeof(m_newChildNameBuf));
	ImGui::SameLine();
	if (ImGui::Button("+ Add Child"))
		addChild();
	ImGui::SameLine();
	ImGui::TextDisabled("(or drag a .pre from Content onto it in the tree above)");

	const bool hasChildren = sc && !sc->children.empty();
	ImGui::BeginDisabled(hasChildren);
	if (ImGui::Button("Remove Scene"))
	{
		m_hasScene = false;
		commitRespawn();
	}
	ImGui::EndDisabled();
	if (hasChildren && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Remove all children first");

	ImGui::PopID();
}

void EntityEditor::renderRenderSection()
{
	if (!m_hasRender)
	{
		if (ImGui::Button("+ Add Render"))
		{
			m_hasRender = true;
			m_renderDraft = RenderComponent::SpawnInfo{};
			commitRespawn();
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("render");

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Container (.oc)");
	ImGui::SameLine(120.0f);
	ImGui::SetNextItemWidth(180.0f);
	inputTextStd("##container", m_renderDraft.containerName);
	const bool containerCommitted = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SameLine();
	const bool containerPicked = namePickerButton("##render_container_pick", m_renderDraft.containerName,
		Globals::assetRegistry.getObjectContainers(), m_renderPickerSearch, sizeof(m_renderPickerSearch));
	if (containerCommitted || containerPicked)
	{
		m_renderDraft.nodePath.clear(); // a node picked from the previous container no longer applies
		commitRespawn();
	}

	// Node list is scoped to whichever container is currently named above — load it (cheap: cached after
	// the first time) only once we know the name resolves to something real, so a half-typed name doesn't
	// spam load-failure warnings every frame.
	std::vector<std::string> nodePaths;
	if (!m_renderDraft.containerName.empty() && Globals::assetRegistry.findObjectContainer(m_renderDraft.containerName))
		if (ObjectContainer* container = Globals::world.getOrLoadContainer(m_renderDraft.containerName))
			nodePaths = container->getNodePaths();

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Node");
	ImGui::SameLine(120.0f);
	ImGui::SetNextItemWidth(180.0f);
	inputTextStd("##node", m_renderDraft.nodePath);
	const bool nodeCommitted = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SameLine();
	ImGui::BeginDisabled(nodePaths.empty());
	const bool nodePicked = nodePickerButton("##render_node_pick", m_renderDraft.nodePath, nodePaths, m_renderNodePickerSearch, sizeof(m_renderNodePickerSearch));
	ImGui::EndDisabled();
	if (nodePaths.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Enter a valid container name first");
	if (nodeCommitted || nodePicked)
		commitRespawn();

	static const char* typeLabels[] = { "StaticMesh", "SkinnedMesh" };
	int typeIdx = m_renderDraft.skinned ? 1 : 0;
	ImGui::AlignTextToFramePadding();
	ImGui::Text("Type");
	ImGui::SameLine(120.0f);
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::Combo("##type", &typeIdx, typeLabels, 2))
	{
		m_renderDraft.skinned = typeIdx == 1;
		commitRespawn();
	}

	if (m_renderDraft.skinned)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Rig");
		ImGui::SameLine(120.0f);
		ImGui::SetNextItemWidth(180.0f);
		inputTextStd("##rig", m_renderDraft.rigType);
		if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
	}

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Local Pos");
	ImGui::SameLine(100.0f);
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::DragFloat3("##localpos", &m_renderDraft.localTransform.pos.x, 0.01f);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Local Scale");
	ImGui::SameLine(100.0f);
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::DragFloat("##localscale", &m_renderDraft.localTransform.scale, 0.01f, 0.0001f, 10000.0f);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

	glm::vec3 euler = glm::degrees(glm::eulerAngles(m_renderDraft.localTransform.quat));
	ImGui::AlignTextToFramePadding();
	ImGui::Text("Local Rot");
	ImGui::SameLine(100.0f);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::DragFloat3("##localrot", &euler.x, 0.5f))
		m_renderDraft.localTransform.quat = glm::quat(glm::radians(euler));
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

	if (ImGui::Button("Remove Render"))
	{
		m_hasRender = false;
		if (m_hasAnimator) // an Animator drives a sibling skinned mesh — remove it too, it'd be meaningless without one
			m_hasAnimator = false;
		commitRespawn();
	}
	ImGui::PopID();
}

void EntityEditor::renderAnimatorSection()
{
	if (!m_hasAnimator)
	{
		ImGui::BeginDisabled(!m_hasRender);
		if (ImGui::Button("+ Add Animator"))
		{
			m_hasAnimator = true;
			m_animatorDraft = AnimatorComponent::SpawnInfo{};
			commitRespawn();
		}
		ImGui::EndDisabled();
		if (!m_hasRender && ImGui::IsItemHovered())
			ImGui::SetTooltip("Needs a Render component (skinned mesh) first");
		return;
	}

	if (!ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("animator");

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Animator");
	ImGui::SameLine(100.0f);
	ImGui::SetNextItemWidth(180.0f);
	inputTextStd("##animname", m_animatorDraft.animatorName);
	const bool nameCommitted = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SameLine();
	const bool picked = namePickerButton("##animator_pick", m_animatorDraft.animatorName,
		Globals::assetRegistry.getAnimators(), m_animatorPickerSearch, sizeof(m_animatorPickerSearch));
	if (nameCommitted || picked)
		commitRespawn();

	if (ImGui::Button("Remove Animator"))
	{
		m_hasAnimator = false;
		commitRespawn();
	}
	ImGui::PopID();
}

void EntityEditor::renderPhysicsSection()
{
	if (!m_hasPhysics)
	{
		if (ImGui::Button("+ Add Physics"))
		{
			m_hasPhysics = true;
			m_physicsDraft = PhysicsComponent::SpawnInfo{};
			m_physicsDraft.bodyType = EPhysicsBodyType::Static;
			m_physCollidesWithBuf[0] = '\0';
			commitRespawn();
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("physics");

	static const char* bodyLabels[] = { "Static", "Kinematic", "Dynamic" };
	int bodyIdx = (int)m_physicsDraft.bodyType;
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::Combo("Body", &bodyIdx, bodyLabels, 3))
	{
		m_physicsDraft.bodyType = (EPhysicsBodyType)bodyIdx;
		commitRespawn();
	}

	static const char* shapeLabels[] = { "Box", "Sphere", "Capsule", "Hull", "Mesh" };
	int shapeIdx = (int)m_physicsDraft.shape.type;
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::Combo("Shape", &shapeIdx, shapeLabels, 5))
	{
		m_physicsDraft.shape.type = (EPhysicsShapeType)shapeIdx;
		commitRespawn();
	}

	switch (m_physicsDraft.shape.type)
	{
	case EPhysicsShapeType::Box:
		ImGui::DragFloat3("Half Extents", &m_physicsDraft.shape.halfExtents.x, 0.01f, 0.001f, 10000.0f);
		if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
		break;
	case EPhysicsShapeType::Sphere:
		ImGui::DragFloat("Radius", &m_physicsDraft.shape.radius, 0.01f, 0.001f, 10000.0f);
		if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
		break;
	case EPhysicsShapeType::Capsule:
		ImGui::DragFloat("Radius", &m_physicsDraft.shape.radius, 0.01f, 0.001f, 10000.0f);
		if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
		ImGui::DragFloat("Half Height", &m_physicsDraft.shape.halfHeight, 0.01f, 0.001f, 10000.0f);
		if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
		break;
	case EPhysicsShapeType::Hull:
		ImGui::DragInt("Max Hull Vertices", &m_physicsDraft.shape.maxHullVertices, 1.0f, 4, 64);
		if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
		ImGui::TextDisabled("Geometry derives from the Render mesh");
		break;
	case EPhysicsShapeType::Mesh:
		ImGui::TextDisabled("Geometry derives from the Render mesh (static bodies only)");
		break;
	}

	ImGui::DragFloat3("Offset", &m_physicsDraft.shape.offset.x, 0.01f);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
	ImGui::DragFloat("Density", &m_physicsDraft.shape.density, 1.0f, 0.0f, 100000.0f);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
	ImGui::DragFloat("Friction", &m_physicsDraft.shape.friction, 0.01f, 0.0f, 10.0f);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
	ImGui::DragFloat("Restitution", &m_physicsDraft.shape.restitution, 0.01f, 0.0f, 1.0f);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

	if (ImGui::Checkbox("Sensor", &m_physicsDraft.shape.isSensor))
		commitRespawn();
	ImGui::SameLine();
	if (ImGui::Checkbox("Contact Events", &m_physicsDraft.shape.contactEvents))
		commitRespawn();

	ImGui::SetNextItemWidth(160.0f);
	inputTextStd("Layer", m_physicsDraft.layer);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::InputTextWithHint("Collides With", "All / None / Layer, Layer, ...", m_physCollidesWithBuf, sizeof(m_physCollidesWithBuf)))
		m_physicsDraft.collidesWith = splitComma(m_physCollidesWithBuf);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragInt("Group", &m_physicsDraft.shape.groupIndex, 1.0f);
	if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

	if (ImGui::Button("Remove Physics"))
	{
		m_hasPhysics = false;
		commitRespawn();
	}
	ImGui::PopID();
}

void EntityEditor::renderAudioSection()
{
	if (!m_hasAudio)
	{
		if (ImGui::Button("+ Add Audio"))
		{
			m_hasAudio = true;
			m_audioDraft = AudioComponent::SpawnInfo{};
			commitRespawn();
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("audio");

	static const char* selectLabels[] = { "Single", "Random", "RandomNoRepeat", "Cycle", "CycleStartRandom" };

	int soundToRemove = -1;
	for (size_t si = 0; si < m_audioDraft.sounds.size(); ++si)
	{
		AudioComponent::SoundDesc& sound = m_audioDraft.sounds[si];
		ImGui::PushID((int)si);
		ImGui::Separator();

		ImGui::SetNextItemWidth(160.0f);
		inputTextStd("Alias", sound.alias);
		if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

		int selectIdx = (int)sound.select;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::Combo("Select", &selectIdx, selectLabels, 5))
		{
			sound.select = (EAudioSelect)selectIdx;
			commitRespawn();
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("Remove Sound"))
			soundToRemove = (int)si;

		int clipToRemove = -1;
		for (size_t ci = 0; ci < sound.clips.size(); ++ci)
		{
			AudioComponent::Clip& clip = sound.clips[ci];
			ImGui::PushID((int)ci);
			ImGui::Indent();

			ImGui::SetNextItemWidth(200.0f);
			inputTextStd("Path", clip.path);
			if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

			ImGui::DragFloat("Volume", &clip.volume, 0.01f, 0.0f, 10.0f);
			if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
			ImGui::DragFloat("Pitch", &clip.pitch, 0.01f, 0.01f, 10.0f);
			if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
			if (ImGui::Checkbox("Loop", &clip.loop)) commitRespawn();
			ImGui::SameLine();
			if (ImGui::Checkbox("Relative (2D)", &clip.relative)) commitRespawn();
			ImGui::DragFloat("Reference Distance", &clip.referenceDistance, 0.1f, 0.0f, 10000.0f);
			if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
			ImGui::DragFloat("Max Distance", &clip.maxDistance, 1.0f, 0.0f, 1000000.0f,
				clip.maxDistance >= FLT_MAX ? "inf" : "%.3f");
			if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();
			ImGui::DragFloat("Rolloff", &clip.rolloff, 0.01f, 0.0f, 10.0f);
			if (ImGui::IsItemDeactivatedAfterEdit()) commitRespawn();

			if (ImGui::SmallButton("Remove Clip"))
				clipToRemove = (int)ci;

			ImGui::Unindent();
			ImGui::PopID();
		}
		if (clipToRemove >= 0)
		{
			sound.clips.erase(sound.clips.begin() + clipToRemove);
			commitRespawn();
		}
		if (ImGui::SmallButton("+ Add Clip"))
		{
			sound.clips.push_back(AudioComponent::Clip{});
			commitRespawn();
		}

		ImGui::PopID();
	}
	if (soundToRemove >= 0)
	{
		m_audioDraft.sounds.erase(m_audioDraft.sounds.begin() + soundToRemove);
		commitRespawn();
	}

	ImGui::Separator();
	if (ImGui::Button("+ Add Sound"))
	{
		AudioComponent::SoundDesc sound;
		sound.alias = "Sound" + std::to_string(m_audioDraft.sounds.size() + 1);
		m_audioDraft.sounds.push_back(std::move(sound));
		commitRespawn();
	}
	ImGui::SameLine();
	if (ImGui::Button("Remove Audio"))
	{
		m_hasAudio = false;
		commitRespawn();
	}
	ImGui::PopID();
}

void EntityEditor::renderParticleSection()
{
	if (!m_hasParticle)
	{
		if (ImGui::Button("+ Add Particle"))
		{
			m_hasParticle = true;
			m_particleDraft = ParticleComponent::SpawnInfo{};
			m_particleEffectBuf[0] = '\0';
			commitRespawn();
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Particle", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("particle");

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Effect");
	ImGui::SameLine(60.0f);
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##effect", "Effects/fire.pfx", m_particleEffectBuf, sizeof(m_particleEffectBuf));
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		m_particleDraft.effectPath = m_particleEffectBuf;
		commitRespawn();
	}

	if (ImGui::Checkbox("Emitting", &m_particleDraft.emitting))
		commitRespawn();

	if (ImGui::Button("Remove Particle"))
	{
		m_hasParticle = false;
		commitRespawn();
	}
	ImGui::PopID();
}

void EntityEditor::renderForceSection()
{
	if (!m_hasForce)
	{
		if (ImGui::Button("+ Add Force"))
		{
			m_hasForce = true;
			m_forceDraft = ForceComponent::SpawnInfo{};
			commitRespawn();
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Force", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("force");

	// Every field respawns the entity on commit (the emitter is created from the SpawnInfo), so edits
	// commit on release rather than per drag frame.
	int team = int(m_forceDraft.team);
	if (ImGui::DragInt("Team", &team, 0.1f, 0, 7)) // Force clamps to MAX_FORCE_TEAMS silently
		m_forceDraft.team = uint32(glm::clamp(team, 0, 7));
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();

	ImGui::DragFloat("Output", &m_forceDraft.output, 0.05f, 0.0f, 1000.0f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();

	ImGui::DragFloat("Reach", &m_forceDraft.reach, 0.05f, 0.01f, 10000.0f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();

	ImGui::SliderFloat("Focus", &m_forceDraft.focus, 0.0f, 1.0f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("0.5 = sphere spanning the reach line, 0/1 = cones pointed at the emitter/target");

	ImGui::SliderFloat("Distribution", &m_forceDraft.distribution, 0.0f, 1.0f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Where the output density sits along the line (0 = emitter end, 1 = target end)");

	ImGui::DragFloat("Width", &m_forceDraft.width, 0.01f, 0.01f, 4.0f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();

	ImGui::DragFloat3("Direction", &m_forceDraft.direction.x, 0.01f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Bubble axis in entity space; only matters when Focus != 0.5");

	ImGui::DragFloat3("Offset", &m_forceDraft.offset.x, 0.01f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		commitRespawn();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Entity-space offset from the anchor — Centered: the bubble centre; otherwise the span start");

	if (ImGui::Checkbox("Centered", &m_forceDraft.centered))
		commitRespawn();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Pull the emitter back half a reach so a zero Offset centres the bubble on the entity");

	if (ImGui::Button("Remove Force"))
	{
		m_hasForce = false;
		commitRespawn();
	}
	ImGui::PopID();
}

void EntityEditor::renderScriptSection()
{
	if (!m_hasScript)
	{
		if (ImGui::Button("+ Add Script"))
		{
			m_hasScript = true;
			m_scriptDraft = ScriptComponent::SpawnInfo{};
			commitRespawn();
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("script");

	ImGui::AlignTextToFramePadding();
	ImGui::Text("Path");
	ImGui::SameLine(60.0f);
	const std::string btnLabel = (m_scriptDraft.scriptPath.empty() ? std::string("<pick a script>") : m_scriptDraft.scriptPath) + "##scriptpick";
	if (ImGui::Button(btnLabel.c_str(), ImVec2(-1.0f, 0.0f)))
	{
		ImGui::OpenPopup("##scriptpicker");
		m_scriptPickerSearch[0] = '\0';
		gatherScriptFiles(m_scriptPickerFiles);
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FILE"))
		{
			m_scriptDraft.scriptPath = static_cast<const char*>(payload->Data);
			commitRespawn();
		}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::BeginPopup("##scriptpicker"))
	{
		ImGui::SetNextItemWidth(280.0f);
		ImGui::InputTextWithHint("##search", "Search...", m_scriptPickerSearch, sizeof(m_scriptPickerSearch));
		ImGui::Separator();
		ImGui::BeginChild("##list", ImVec2(280.0f, 200.0f));
		for (const std::string& path : m_scriptPickerFiles)
		{
			if (!containsCI(path, m_scriptPickerSearch))
				continue;
			if (ImGui::Selectable(path.c_str(), path == m_scriptDraft.scriptPath))
			{
				m_scriptDraft.scriptPath = path;
				commitRespawn();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}

	if (ImGui::Checkbox("Enabled", &m_scriptDraft.enabled))
		commitRespawn();

	if (ImGui::Button("Remove Script"))
	{
		m_hasScript = false;
		commitRespawn();
	}
	ImGui::PopID();
}

void EntityEditor::renderComponents()
{
	ImGui::SeparatorText("Components");
	renderSceneSection();
	renderRenderSection();
	renderAnimatorSection();
	renderPhysicsSection();
	renderAudioSection();
	renderParticleSection();
	renderForceSection();
	renderScriptSection();
}

void EntityEditor::commitRespawn()
{
	if (!m_selected)
		return;

	auto tmpl = std::make_shared<EntitySpawnTemplate>();
	uint16 typeBits = 0;
	std::vector<std::shared_ptr<void>> infos;

	// Scene (bit 0) — driven by the m_hasScene draft flag (toggled by Add/Remove Scene), not the live
	// entity's current state. main.cpp splices any existing children from the old entity onto the
	// respawned one, so they survive even though this editor rebuilds the component set from scratch.
	if (m_hasScene)
	{
		typeBits |= uint16(1 << EComponentID_Scene);
		infos.push_back(std::make_shared<SceneComponent::SpawnInfo>());
	}

	const std::string ownerName = m_selected->hasName() ? std::string(m_selected->getName()) : currentId();

	std::string renderContainerName, renderNodePath;
	if (m_hasRender)
	{
		AssetNode holder;
		AssetNode& node = holder.addChild("Component");
		node.values.emplace_back("Render");
		if (!m_renderDraft.containerName.empty())
		{
			node.set("ObjectContainer", m_renderDraft.containerName);
			if (!m_renderDraft.nodePath.empty())
				node.set("Node", m_renderDraft.nodePath);
			AssetNode& typeNode = node.addChild("Type");
			typeNode.values.emplace_back(m_renderDraft.skinned ? "SkinnedMesh" : "StaticMesh");
			if (m_renderDraft.skinned && !m_renderDraft.rigType.empty())
				typeNode.addChild("Rig").values.emplace_back(m_renderDraft.rigType);
		}
		node.set("Position", m_renderDraft.localTransform.pos);
		node.set("Rotation", glm::degrees(glm::eulerAngles(m_renderDraft.localTransform.quat)));
		node.set("Scale", m_renderDraft.localTransform.scale);

		const bool wantsCollisionGeometry = m_hasPhysics &&
			(m_physicsDraft.shape.type == EPhysicsShapeType::Hull || m_physicsDraft.shape.type == EPhysicsShapeType::Mesh);
		if (auto info = Globals::world.buildRenderSpawnInfo(node, ownerName, wantsCollisionGeometry))
		{
			renderContainerName = info->containerName;
			renderNodePath = info->nodePath;
			typeBits |= uint16(1 << EComponentID_Render);
			infos.push_back(std::move(info));
		}
	}

	if (m_hasAnimator && !renderContainerName.empty())
	{
		AssetNode holder;
		AssetNode& node = holder.addChild("Component");
		node.values.emplace_back("Animator");
		node.set("Animator", m_animatorDraft.animatorName);
		if (auto info = Globals::world.buildAnimatorSpawnInfo(node, renderContainerName, ownerName))
		{
			typeBits |= uint16(1 << EComponentID_Animator);
			infos.push_back(std::move(info));
		}
	}

	if (m_hasPhysics)
	{
		AssetNode holder;
		AssetNode& node = holder.addChild("Component");
		node.values.emplace_back("Physics");

		static const char* bodyTokens[] = { "Static", "Kinematic", "Dynamic" };
		node.set("Body", bodyTokens[(int)m_physicsDraft.bodyType]);

		switch (m_physicsDraft.shape.type)
		{
		case EPhysicsShapeType::Box:
			node.set("Shape", "Box");
			node.set("HalfExtents", m_physicsDraft.shape.halfExtents);
			break;
		case EPhysicsShapeType::Sphere:
			node.set("Shape", "Sphere");
			node.set("Radius", m_physicsDraft.shape.radius);
			break;
		case EPhysicsShapeType::Capsule:
			node.set("Shape", "Capsule");
			node.set("Radius", m_physicsDraft.shape.radius);
			node.set("HalfHeight", m_physicsDraft.shape.halfHeight);
			break;
		case EPhysicsShapeType::Hull:
			node.set("Shape", "Hull");
			node.addChild("MaxHullVertices").values.emplace_back(std::to_string(m_physicsDraft.shape.maxHullVertices));
			break;
		case EPhysicsShapeType::Mesh:
			node.set("Shape", "Mesh");
			break;
		}

		node.set("Offset", m_physicsDraft.shape.offset);
		node.set("Density", m_physicsDraft.shape.density);
		node.set("Friction", m_physicsDraft.shape.friction);
		node.set("Restitution", m_physicsDraft.shape.restitution);
		node.set("Sensor", m_physicsDraft.shape.isSensor);
		node.set("ContactEvents", m_physicsDraft.shape.contactEvents);
		if (!m_physicsDraft.layer.empty())
			node.set("Layer", m_physicsDraft.layer);
		if (!m_physicsDraft.collidesWith.empty())
		{
			AssetNode& cw = node.addChild("CollidesWith");
			cw.values = m_physicsDraft.collidesWith;
		}
		if (m_physicsDraft.shape.groupIndex != 0)
			node.addChild("Group").values.emplace_back(std::to_string(m_physicsDraft.shape.groupIndex));

		if (auto info = Globals::world.buildPhysicsSpawnInfo(node, renderContainerName, renderNodePath, ownerName))
		{
			typeBits |= uint16(1 << EComponentID_Physics);
			infos.push_back(std::move(info));
		}
	}

	if (m_hasAudio)
	{
		AssetNode holder;
		AssetNode& node = holder.addChild("Component");
		node.values.emplace_back("Audio");
		for (const AudioComponent::SoundDesc& sound : m_audioDraft.sounds)
		{
			AssetNode& sn = node.addChild("Sound");
			sn.values.emplace_back(sound.alias);
			AssetNode& selNode = sn.addChild("Select");
			selNode.values.emplace_back(audioSelectToken(sound.select));
			for (const AudioComponent::Clip& clip : sound.clips)
			{
				AssetNode& pn = sn.addChild("Path");
				pn.values.emplace_back(clip.path);
				pn.set("Volume", clip.volume);
				pn.set("Pitch", clip.pitch);
				pn.set("Loop", clip.loop);
				pn.set("Relative", clip.relative);
				pn.set("ReferenceDistance", clip.referenceDistance);
				pn.set("MaxDistance", clip.maxDistance);
				pn.set("Rolloff", clip.rolloff);
			}
		}
		if (auto info = Globals::world.buildAudioSpawnInfo(node, ownerName))
		{
			typeBits |= uint16(1 << EComponentID_Audio);
			infos.push_back(std::move(info));
		}
	}

	if (m_hasParticle && !m_particleDraft.effectPath.empty())
	{
		auto info = std::make_shared<ParticleComponent::SpawnInfo>(m_particleDraft);
		typeBits |= uint16(1 << EComponentID_Particle);
		infos.push_back(std::move(info));
	}

	if (m_hasForce)
	{
		auto info = std::make_shared<ForceComponent::SpawnInfo>(m_forceDraft);
		typeBits |= uint16(1 << EComponentID_Force);
		infos.push_back(std::move(info));
	}

	if (m_hasScript)
	{
		auto info = std::make_shared<ScriptComponent::SpawnInfo>();
		info->scriptPath = m_scriptDraft.scriptPath;
		info->enabled = m_scriptDraft.enabled;
		typeBits |= uint16(1 << EComponentID_Script);
		infos.push_back(std::move(info));
	}

	tmpl->archetype = makeEntityArchetype(typeBits);
	tmpl->spawnInfos = std::move(infos);
	tmpl->displayName = m_selected->getName();
	tmpl->enabled = m_selected->isEnabled(); // carry the live enable state onto the respawned entity
	tmpl->sourceFile = m_path; // every node in the document belongs to the open .pre (empty until first save)
	// Keep the prefab identity across respawns — losing it would break "Open Selected"'s registry lookup
	// and re-serialize the entity inline instead of as a "Prefab <name>" reference.
	tmpl->prefabName = (m_selected.get() == m_editRoot.get() && !m_path.empty())
		? std::filesystem::path(m_path).stem().string()
		: m_selected->getPrefabName();

	m_changes.push_back({ EntityChange::RespawnEntity{ m_selected, tmpl } });
}

void EntityEditor::render(Entity* sceneSelection)
{
	m_sceneSelection = sceneSelection;

	renderToolbar();
	ImGui::Separator();

	if (!m_editRoot)
		ImGui::TextDisabled("No entity open.");
	else
	{
		renderTree();
		ImGui::Separator();
		renderNameAndTransform();
		ImGui::Separator();
		renderComponents();
	}

	renderOverwritePopup();
	renderUnsavedPopup();
}
