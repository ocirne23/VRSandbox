export module UI.AssetBrowser;

import Core;

export class AssetBrowser
{
public:

	AssetBrowser() {}
	void initialize();
	void render();

	void setRootPath(const std::filesystem::path& path);
	const std::filesystem::path& getSelectedPath() const { return m_selectedPath; }
	bool hasSelection() const { return !m_selectedPath.empty(); }

private:

	void renderToolbar();
	void renderDirectoryTree(const std::filesystem::path& dir);
	void renderContentGrid();
	void renderContentList();
	void acceptPrefabDrop();
	void renderContextMenu(const std::filesystem::path& p);
	void navigateTo(const std::filesystem::path& path);
	void navigateUp();

	std::filesystem::path m_rootPath;
	std::filesystem::path m_currentPath;
	std::filesystem::path m_selectedPath;

	char  m_searchBuf[256] = {};
	float m_leftPaneWidth  = 220.0f;
	float m_iconSize       = 72.0f;
	bool  m_listView       = false;
};