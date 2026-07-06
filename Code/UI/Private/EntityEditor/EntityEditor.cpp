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

std::string EntityEditor::currentId() const
{
	if (!m_path.empty())
		return std::filesystem::path(m_path).stem().string();
	return (m_editRoot && !m_editRoot->displayName.empty()) ? m_editRoot->displayName : std::string("NewEntity");
}

void EntityEditor::rebaseline()
{
	m_baselineText = m_editRoot ? serializePrefabText(m_editRoot.get(), currentId()) : std::string();
}

bool EntityEditor::isDirty() const
{
	if (!m_editRoot)
		return false;
	return serializePrefabText(m_editRoot.get(), currentId()) != m_baselineText;
}

void EntityEditor::refreshDraftsFromEntity()
{
	Entity* e = m_editRoot.get();

	m_hasRender = hasComponent<RenderComponent>(e);
	m_renderDraft = m_hasRender ? *getRenderSpawnInfo(e) : RenderComponent::SpawnInfo{};

	m_hasAnimator = hasComponent<AnimatorComponent>(e);
	m_animatorDraft = m_hasAnimator ? *getAnimatorSpawnInfo(e) : AnimatorComponent::SpawnInfo{};

	m_hasPhysics = hasComponent<PhysicsComponent>(e);
	m_physicsDraft = m_hasPhysics ? *getPhysicsSpawnInfo(e) : PhysicsComponent::SpawnInfo{};
	strncpy_s(m_physCollidesWithBuf, sizeof(m_physCollidesWithBuf), joinComma(m_physicsDraft.collidesWith).c_str(), sizeof(m_physCollidesWithBuf) - 1);

	m_hasAudio = hasComponent<AudioComponent>(e);
	m_audioDraft = m_hasAudio ? *getAudioSpawnInfo(e) : AudioComponent::SpawnInfo{};

	m_hasScript = hasComponent<ScriptComponent>(e);
	m_scriptDraft = ScriptComponent::SpawnInfo{};
	if (m_hasScript)
	{
		if (ScriptComponent* sc = getComponent<ScriptComponent>(e))
		{
			m_scriptDraft.scriptPath = sc->scriptModule ? sc->scriptModule->scriptPath : std::string();
			m_scriptDraft.enabled = sc->enabled;
		}
	}
}

void EntityEditor::onOpened(EntityPtr root, const std::string& path)
{
	m_editRoot = root;
	m_path = path;

	std::string suggested = path;
	if (suggested.empty() && root)
		suggested = "Entities/" + (root->displayName.empty() ? std::string("NewEntity") : root->displayName) + ".pre";
	strncpy_s(m_pathBuf, sizeof(m_pathBuf), suggested.c_str(), sizeof(m_pathBuf) - 1);
	m_pathBuf[sizeof(m_pathBuf) - 1] = '\0';

	refreshDraftsFromEntity();
	rebaseline();
}

void EntityEditor::onRespawned(EntityPtr oldEntity, EntityPtr newEntity)
{
	if (m_editRoot.get() != oldEntity.get())
		return; // stale — the tracked entity has already moved on (e.g. closed while this was in flight)
	m_editRoot = newEntity;
	// Deliberately NOT calling refreshDraftsFromEntity() here: the draft state (m_hasX/m_xDraft) is what
	// drove this respawn and stays authoritative. A component whose builder legitimately returned null
	// (e.g. Render with no mesh picked yet, Audio with no clips yet) should keep showing what the user
	// typed so far rather than silently reverting — it just won't be part of the entity until valid.
}

void EntityEditor::closeCurrent()
{
	if (m_editRoot)
		m_changes.push_back({ EntityChange::Delete{ m_editRoot } });
	m_editRoot = EntityPtr();
	m_path.clear();
	m_baselineText.clear();
	m_hasRender = m_hasAnimator = m_hasPhysics = m_hasAudio = m_hasScript = false;
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

void EntityEditor::requestOpen(const std::string& path)
{
	if (path.empty())
		return;
	if (m_editRoot && isDirty())
	{
		m_pendingSwitchIsNew = false;
		m_pendingSwitchPath  = path;
		m_openUnsavedPopup   = true;
		return;
	}
	doSwitchOpen(path);
}

void EntityEditor::requestNew(const std::string& name)
{
	if (m_editRoot && isDirty())
	{
		m_pendingSwitchIsNew = true;
		m_pendingSwitchName  = name;
		m_openUnsavedPopup   = true;
		return;
	}
	doSwitchNew(name);
}

void EntityEditor::queueSave(const std::string& path)
{
	m_changes.push_back({ EntityChange::SavePrefab{ m_editRoot, path } });
	m_path = path;
	rebaseline();
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
		if (m_pendingSwitchIsNew)
			ImGui::TextUnformatted("Start a new entity anyway?");
		else
			ImGui::Text("Switch to '%s'?", m_pendingSwitchPath.c_str());
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
			if (m_pendingSwitchIsNew)
				doSwitchNew(m_pendingSwitchName);
			else
				doSwitchOpen(m_pendingSwitchPath);
		}
		if (save || discard || cancel)
		{
			m_pendingSwitchPath.clear();
			m_pendingSwitchName.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void EntityEditor::renderToolbar()
{
	if (ImGui::Button("New"))
	{
		static int counter = 0;
		requestNew("NewEntity" + std::to_string(++counter));
	}
	ImGui::SameLine();

	ImGui::SetNextItemWidth(240.0f);
	ImGui::InputTextWithHint("##ee_path", "Entities/MyEntity.pre", m_pathBuf, sizeof(m_pathBuf));
	ImGui::SameLine();
	if (ImGui::Button("Open"))
		requestOpen(m_pathBuf);
	ImGui::SameLine();

	ImGui::BeginDisabled(!m_editRoot);
	if (ImGui::Button("Save"))
		trySave(m_pathBuf);
	ImGui::EndDisabled();

	if (m_editRoot)
	{
		ImGui::SameLine();
		ImGui::TextDisabled(isDirty() ? "* unsaved changes" : "saved");
	}
}

void EntityEditor::renderNameAndTransform()
{
	strncpy_s(m_nameBuf, sizeof(m_nameBuf), m_editRoot->displayName.c_str(), sizeof(m_nameBuf) - 1);
	m_nameBuf[sizeof(m_nameBuf) - 1] = '\0';
	ImGui::SetNextItemWidth(240.0f);
	if (ImGui::InputText("Name", m_nameBuf, sizeof(m_nameBuf)))
		m_editRoot->displayName = m_nameBuf;

	ImGui::DragFloat3("Position", &m_editRoot->pos.x, 0.05f);
	ImGui::DragFloat("Scale", &m_editRoot->scale, 0.01f, 0.0001f, 10000.0f);

	glm::vec3 euler = glm::degrees(glm::eulerAngles(m_editRoot->rot));
	if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
		m_editRoot->rot = glm::quat(glm::radians(euler));
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
	ImGui::Text("Mesh");
	ImGui::SameLine(100.0f);
	ImGui::SetNextItemWidth(180.0f);
	inputTextStd("##mesh", m_renderDraft.spawnableName);
	const bool nameCommitted = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SameLine();
	const bool picked = namePickerButton("##render_pick", m_renderDraft.spawnableName,
		Globals::assetRegistry.getSpawnables(), m_renderPickerSearch, sizeof(m_renderPickerSearch));
	if (nameCommitted || picked)
	{
		if (const SpawnableDesc* desc = Globals::assetRegistry.findSpawnable(m_renderDraft.spawnableName))
			m_renderDraft.skinned = desc->skinned;
		commitRespawn();
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
			ImGui::DragFloat("Max Distance", &clip.maxDistance, 1.0f, 0.0f, 1000000.0f);
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
	ImGui::SetNextItemWidth(-1.0f);
	inputTextStd("##scriptpath", m_scriptDraft.scriptPath);
	const bool committed = ImGui::IsItemDeactivatedAfterEdit();
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FILE"))
		{
			m_scriptDraft.scriptPath = static_cast<const char*>(payload->Data);
			commitRespawn();
		}
		ImGui::EndDragDropTarget();
	}
	if (committed)
		commitRespawn();

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
	renderRenderSection();
	renderAnimatorSection();
	renderPhysicsSection();
	renderAudioSection();
	renderScriptSection();
}

void EntityEditor::commitRespawn()
{
	if (!m_editRoot)
		return;

	auto tmpl = std::make_shared<EntitySpawnTemplate>();
	uint16 typeBits = 0;
	std::vector<std::shared_ptr<void>> infos;

	// Scene (bit 0) — preserved only if the entity already carries one; main.cpp splices its existing
	// children onto the respawned entity, this editor doesn't build hierarchy itself.
	if (hasComponent<SceneComponent>(m_editRoot.get()))
	{
		typeBits |= uint16(1 << EComponentID_Scene);
		auto info = std::make_shared<SceneComponent::SpawnInfo>();
		if (SceneComponent* sc = getComponent<SceneComponent>(m_editRoot.get()))
			info->enabled = sc->enabled;
		infos.push_back(std::move(info));
	}

	const std::string ownerName = currentId();

	std::string renderContainerName, renderNodePath;
	if (m_hasRender)
	{
		AssetNode holder;
		AssetNode& node = holder.addChild("Component");
		node.values.emplace_back("Render");
		if (!m_renderDraft.spawnableName.empty())
			node.set(m_renderDraft.skinned ? "SkinnedMesh" : "StaticMesh", m_renderDraft.spawnableName);
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
	tmpl->displayName = m_editRoot->displayName;

	m_changes.push_back({ EntityChange::RespawnEntity{ m_editRoot, tmpl } });
}

void EntityEditor::render()
{
	renderToolbar();
	ImGui::Separator();

	if (!m_editRoot)
		ImGui::TextDisabled("No entity open. Click New, type a path and Open, or drag a .pre file here.");
	else
	{
		renderNameAndTransform();
		ImGui::Separator();
		renderComponents();
	}

	renderOverwritePopup();
	renderUnsavedPopup();
}
