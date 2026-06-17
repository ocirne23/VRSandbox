export module UI.SceneView;

import Core;
import Core.Transform;
import Entity;
import Entity.World;
import Entity.Component;

// Entity-backed scene hierarchy panel. There is no persistent UI tree: every frame the panel renders
// straight from Globals::entityRegistry, so it is always in sync with the live entities. SceneComponent
// entities live under a synthetic "World" node and can hold children; entities without one ("loose")
// are listed as siblings of World and are always leaves, but can still be dragged under a scene entity
// to become its child. Mutations (create / delete / reparent / rename) are deferred until after the
// tree is walked.
export class SceneView
{
public:

	void render(const std::vector<EntityPtr>& rootEntities);

	Entity* getSelected() const          { return m_selected; }
	void    setSelected(Entity* entity)  { m_selected = entity; }

	// Mutations made through the panel since the last call (drain once per frame). The panel performs
	// the scene-graph change itself (create / delete / reparent); each EntityChange reports it to the
	// app so it can spawn dropped assets (CreateHierarchy) and reconcile its own root-entity list
	// against Delete/Reparent. The owning handles inside keep entities alive across the hand-off.
	std::vector<EntityChange> takeChanges() { return std::move(m_changes); }

private:

	void renderToolbar();
	void renderEntityNode(Entity* entity);  // scene entity (recurses children) or loose leaf
	void renderContextMenu(Entity* entity);
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

	// Deferred mutations (applied after the full tree render, never mid-walk).
	Entity* m_pendingDelete         = nullptr;
	bool    m_hasPendingReparent    = false;
	Entity* m_pendingReparentChild  = nullptr;
	Entity* m_pendingReparentTarget = nullptr;  // nullptr → World root
	bool    m_hasPendingCreate      = false;
	Entity* m_pendingCreateParent   = nullptr;  // nullptr → new root, else new child of this
};
