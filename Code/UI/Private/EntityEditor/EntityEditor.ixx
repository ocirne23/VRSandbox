export module UI:EntityEditor;

import Core;
import Entity;

// Panel for defining an entity's components (Render/Animator/Physics/Audio/Script) and saving/loading it
// as a .pre file, instead of hand-editing the text. The entity being edited is spawned as a real root
// entity in the running world (like everything else in this engine) and unpacked so it's freely editable.
//
// Components are inline-allocated in one buffer sized by typeBits at entity-creation time — there is no
// live add/remove/resize API. So adding/removing a component, or committing a field edit, assembles a
// fresh EntitySpawnTemplate from the entity's current component set and respawns it in place (see
// commitRespawn()). This reuses the exact machinery Save/Load already rely on (World's buildXSpawnInfo
// resolvers), just driven directly instead of through a file.
export class EntityEditor
{
public:

	void render();

	// Called (via UI) once main.cpp has fulfilled an OpenPrefabForEdit/NewPrefab EntityChange and the
	// entity actually exists in the world's root list.
	void onOpened(EntityPtr root, const std::string& path);

	// Called once main.cpp has fulfilled a RespawnEntity change (a component was added/removed/edited).
	void onRespawned(EntityPtr oldEntity, EntityPtr newEntity);

	// Queues an open/new request, going through the unsaved-changes guard first. Safe to call from the
	// toolbar, a drag-drop of ASSET_FILE onto the panel, or the asset browser's "Edit Entity" action.
	void requestOpen(const std::string& path);
	void requestNew(const std::string& name);

	std::vector<EntityChange> takeChanges() { return std::move(m_changes); }

private:

	void renderToolbar();
	void renderOverwritePopup();
	void renderUnsavedPopup();
	void renderTree();
	void renderTreeNode(Entity* node);
	void renderNameAndTransform();
	void renderComponents();
	void renderSceneSection();
	void renderRenderSection();
	void renderAnimatorSection();
	void renderPhysicsSection();
	void renderAudioSection();
	void renderScriptSection();

	void doSwitchOpen(const std::string& path);
	void doSwitchNew(const std::string& name);
	void closeCurrent(); // detaches m_editRoot bookkeeping; still queues its Delete

	void trySave(const std::string& path);   // overwrite-confirms if the target file already exists
	void queueSave(const std::string& path);

	bool isDirty() const;
	void rebaseline();
	std::string currentId() const; // filename stem once saved/opened, else the root's display name

	void selectEntity(EntityPtr entity); // switches which entity Name/Transform/Components edits
	void addChild();                     // creates a blank inline child under m_selected (name box)
	void removeChild(Entity* child);

	void refreshDraftsFromEntity(); // re-reads every component's current SpawnInfo from m_selected

	// Assembles a fresh EntitySpawnTemplate from the current draft state and queues a RespawnEntity for
	// m_selected (not necessarily the document root — any node in the tree can be edited/respawned).
	void commitRespawn();

	EntityPtr   m_editRoot; // the document's root entity
	EntityPtr   m_selected; // entity currently shown in Name/Transform/Components (root or a descendant)
	std::string m_path; // empty until first save/open

	std::string m_baselineText; // serialized text at last load/save, for dirty-tracking

	std::vector<EntityChange> m_changes;

	char m_pathBuf[256] = "Entities/NewEntity.pre";
	char m_nameBuf[256] = {};

	bool        m_pendingSwitchIsNew = false;
	std::string m_pendingSwitchPath;
	std::string m_pendingSwitchName;
	bool        m_openUnsavedPopup = false;

	bool        m_openOverwritePopup = false;
	std::string m_pendingSavePath;

	// --- Component presence + draft state, for m_selected. Drafts hold the real SpawnInfo types directly
	// (they're fully visible here since Entity.ixx exports :Component) so widgets bind straight to their
	// fields; commit (Add/Remove, or a field's IsItemDeactivatedAfterEdit) calls commitRespawn(). ---
	bool m_hasScene    = false;
	bool m_hasRender   = false;
	bool m_hasAnimator = false;
	bool m_hasPhysics  = false;
	bool m_hasAudio    = false;
	bool m_hasScript   = false;

	RenderComponent::SpawnInfo   m_renderDraft;
	AnimatorComponent::SpawnInfo m_animatorDraft;
	PhysicsComponent::SpawnInfo  m_physicsDraft;
	AudioComponent::SpawnInfo    m_audioDraft;
	ScriptComponent::SpawnInfo   m_scriptDraft;

	char m_physCollidesWithBuf[256] = {}; // comma-joined display/edit buffer for physicsDraft.collidesWith

	char m_renderPickerSearch[128]   = {};
	char m_animatorPickerSearch[128] = {};

	char m_newChildNameBuf[128] = "Entity";
	Entity* m_pendingRemoveChild = nullptr; // set while walking the tree, applied after (avoids mutating mid-walk)
};
