export module UI:AssetBrowser;

import Core;
import Entity;

export class AssetBrowser
{
public:

	AssetBrowser() {}
	void initialize();
	void render();

	void selectFile(const std::filesystem::path& path); // navigate to the file's folder and select it

	const std::filesystem::path& getSelectedPath() const { return m_selectedPath; }
	bool hasSelection() const { return !m_selectedPath.empty(); }

	std::vector<EntityChange> takeChanges() { return std::move(m_changes); }

	// Script (.scr) files: the UI drains these and drives the Script panel / Scene.
	std::string takeScriptOpenRequest()   { return std::move(m_scriptOpenRequest); }
	std::string takeScriptCreateRequest() { return std::move(m_scriptCreateRequest); }

	// .pre files: the UI drains this and drives the Entity Editor panel.
	std::string takeEntityEditRequest() { return std::move(m_entityEditRequest); }

private:

	void renderToolbar();
	void renderDirectoryTree(const std::filesystem::path& dir);
	void renderContentGrid();
	void renderContentList();
	void acceptPrefabDrop();
	void renderOverwritePopup();
	void renderCyclePopup();
	void renderRenamePopup();
	void renderNewAssetContextMenu();
	void queueSavePrefab(Entity* root, const std::filesystem::path& path);
	void renderContextMenu(const std::filesystem::path& p);
	std::filesystem::path makeUniqueAssetPath(const char* stem, const char* ext) const;
	void navigateTo(const std::filesystem::path& path);
	void navigateUp();

	bool isWithinRoot(const std::filesystem::path& path) const;

	std::filesystem::path m_rootPath;
	std::filesystem::path m_currentPath;
	std::filesystem::path m_selectedPath;

	std::vector<EntityChange> m_changes;          // prefab saves queued for the app to drain
	std::string           m_scriptOpenRequest;     // .scr the user asked to open (drained by UI)
	std::string           m_scriptCreateRequest;   // .scr the user asked to create (drained by UI)
	std::string           m_entityEditRequest;       // .pre the user asked to edit (drained by UI)
	EntityPtr             m_pendingSaveRoot;       // entity awaiting overwrite confirmation (kept alive)
	std::filesystem::path m_pendingSavePath;       // target .pre for the pending save
	bool                  m_openOverwritePopup = false;
	bool                  m_openCyclePopup     = false;

	std::filesystem::path m_renameTarget;          // file/folder awaiting rename
	bool                  m_openRenamePopup = false;
	char                  m_renameBuf[256] = {};

	char  m_searchBuf[256] = {};
	float m_leftPaneWidth  = 220.0f;
	float m_iconSize       = 72.0f;
	bool  m_listView       = false;
};