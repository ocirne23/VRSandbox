export module UI.SceneView;

import Core;
import Core.Transform;
import Entity;
import Entity.Component;
import Entity.Registry;

// Entity-backed scene hierarchy panel. There is no persistent UI tree: every frame the panel renders
// straight from Globals::entityRegistry, so it is always in sync with the live entities. SceneComponent
// entities live under a synthetic "World" node and can hold children; entities without one ("loose")
// are listed as siblings of World and are always leaves, but can still be dragged under a scene entity
// to become its child. Mutations (create / delete / reparent / rename) are deferred until after the
// tree is walked.
export class SceneView
{
public:

	void render();

	Entity* getSelected() const          { return m_selected; }
	void    setSelected(Entity* entity)  { m_selected = entity; }

	// Entities deleted via the panel since the last call (drain once per frame, like takeAssetDrops).
	// The returned handles keep each entity alive until the caller drops them, letting owners outside
	// the scene graph (e.g. the app's root-entity list) drop their own handle by matching pointers.
	std::vector<EntityPtr> takeDeletedEntities() { return std::move(m_deletedEntities); }

private:

	void renderToolbar();
	void renderWorld();
	void renderEntityNode(Entity* entity);  // scene entity (recurses children) or loose leaf
	void renderContextMenu(Entity* entity);
	void beginRename(Entity* entity);

	bool dragSourceFor(Entity* entity);     // returns true while this entity is being dragged
	void dropTargetReparentUnder(Entity* parent); // accepts a dragged scene entity, reparents under `parent`

	void applyPendingMutations();

	std::vector<EntityPtr> m_deletedEntities; // queued for the app to poll (keeps them alive meanwhile)

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
