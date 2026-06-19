export module UI.SceneView;

import Core;
import Core.Transform;
import Entity;

export class SceneView
{
public:

	void render(const std::vector<EntityPtr>& rootEntities);

	Entity* getSelected() const          { return m_selected; }
	void    setSelected(Entity* entity)  { m_selected = entity; }

	std::vector<EntityChange> takeChanges() { return std::move(m_changes); }

private:

	void renderToolbar();
	void renderEntityNode(Entity* entity, bool ancestorLocked);  // scene entity (recurses children) or loose leaf
	void renderContextMenu(Entity* entity, bool locked);
	void beginRename(Entity* entity);

	bool dragSourceFor(Entity* entity);     // returns true while this entity is being dragged
	void dropTargetReparentUnder(Entity* parent); // accepts a dragged scene entity, reparents under `parent`
	void acceptAssetSpawnPayload(Entity* parent); // queues an "ASSET_FILE" drop to spawn under `parent`

	void applyPendingMutations();

	std::vector<EntityChange> m_changes;                            // panel mutations queued for the app to drain

	Entity* m_selected       = nullptr;
	Entity* m_renamingEntity = nullptr;
	bool    m_focusRenameNext = false;
	bool    m_worldOpen       = true;
	char    m_renameBuffer[256] = {};
	char    m_searchBuffer[256] = {};
	int     m_entityCounter   = 0;

	Entity* m_pendingDelete         = nullptr;
	bool    m_hasPendingReparent    = false;
	Entity* m_pendingReparentChild  = nullptr;
	Entity* m_pendingReparentTarget = nullptr;  // nullptr â†’ World root
	bool    m_hasPendingCreate      = false;
	Entity* m_pendingCreateParent   = nullptr;  // nullptr â†’ new root, else new child of this
};
