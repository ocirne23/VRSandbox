export module UI.SceneView;

import Core;

export struct SceneNode
{
	std::string name;
	bool enabled = true;
	uint64 entityId = 0;          // 0 = no bound entity
	SceneNode* parent = nullptr;
	std::vector<std::unique_ptr<SceneNode>> children;
};

export class SceneView
{
public:

	void render();

	SceneNode* addNode(SceneNode* parent, std::string name);
	void       removeNode(SceneNode* node);
	SceneNode* duplicateNode(SceneNode* node);

	SceneNode*       getSelected() const  { return m_selected; }
	void             setSelected(SceneNode* node) { m_selected = node; }

private:

	void renderToolbar();
	void renderNode(SceneNode& node);
	void renderContextMenu(SceneNode* node);   // nullptr = empty-space menu
	void beginRename(SceneNode* node);
	void deleteNode(SceneNode* node);
	void reparentNode(SceneNode* node, SceneNode* newParent);

	std::vector<std::unique_ptr<SceneNode>>& containerOf(SceneNode* node);

	std::vector<std::unique_ptr<SceneNode>> m_roots;

	SceneNode* m_selected          = nullptr;
	SceneNode* m_renamingNode      = nullptr;
	bool       m_focusRenameNext   = false;
	char       m_renameBuffer[256] = {};
	char       m_searchBuffer[256] = {};
	int        m_entityCounter     = 0;

	// Deferred mutations (safe to apply after the full tree render)
	SceneNode* m_pendingDelete          = nullptr;
	bool       m_hasPendingReparent     = false;
	SceneNode* m_pendingReparentNode    = nullptr;
	SceneNode* m_pendingReparentTarget  = nullptr;  // nullptr → move to root
};
