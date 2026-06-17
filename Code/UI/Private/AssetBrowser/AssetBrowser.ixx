export module UI.AssetBrowser;

import Core;
import Entity;

export class AssetBrowser
{
public:

	AssetBrowser() {}
	void initialize();
	void render();

	const std::filesystem::path& getSelectedPath() const { return m_selectedPath; }
	bool hasSelection() const { return !m_selectedPath.empty(); }

	// Prefab saves requested by dropping a scene entity here since the last call (drained once per frame
	// by the app, which owns the World and writes the .pre). Each carries an owning handle to the entity.
	std::vector<EntityChange> takeChanges() { return std::move(m_changes); }

private:

	void renderToolbar();
	void renderDirectoryTree(const std::filesystem::path& dir);
	void renderContentGrid();
	void renderContentList();
	void acceptPrefabDrop();
	void renderOverwritePopup();
	void queueSavePrefab(Entity* root, const std::filesystem::path& path);
	void renderContextMenu(const std::filesystem::path& p);
	void navigateTo(const std::filesystem::path& path);
	void navigateUp();

	// True if `path` is the root or lives inside it. The browser never navigates outside the root,
	// so the Assets folder (set at init) acts as a hard floor.
	bool isWithinRoot(const std::filesystem::path& path) const;

	std::filesystem::path m_rootPath;
	std::filesystem::path m_currentPath;
	std::filesystem::path m_selectedPath;

	std::vector<EntityChange> m_changes;          // prefab saves queued for the app to drain
	EntityPtr             m_pendingSaveRoot;       // entity awaiting overwrite confirmation (kept alive)
	std::filesystem::path m_pendingSavePath;       // target .pre for the pending save
	bool                  m_openOverwritePopup = false;

	char  m_searchBuf[256] = {};
	float m_leftPaneWidth  = 220.0f;
	float m_iconSize       = 72.0f;
	bool  m_listView       = false;
};