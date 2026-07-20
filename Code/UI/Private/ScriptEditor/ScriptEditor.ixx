export module UI:ScriptEditor;

import Core;

// Standalone plain-text (.txt) file editor panel ("Script Editor" window) — a plain file editor with
// standard text-editing functionality (selection, copy/cut/paste, undo/redo, word/line navigation, all
// native to ImGui's multiline input). Unrelated to the visual Script (Node Editor) panel.
export class ScriptEditor
{
public:

	void render();

	// Queues an open/new request, going through the unsaved-changes guard first. Safe to call from the
	// toolbar, a drag-drop of ASSET_FILE onto the panel, or the asset browser's "Open Text File" action.
	void requestOpen(const std::string& path);
	void requestNew();

	bool isDirty() const { return m_hasDoc && m_text != m_baselineText; }
	const std::string& path() const { return m_path; }

private:

	void renderToolbar();
	void renderUnsavedPopup();
	void renderSaveAsPopup();

	void doOpen(const std::string& path);
	void doNew();
	void save();      // writes to m_path if set, else opens the Save As popup
	void saveAs(const std::string& path);

	bool        m_hasDoc = false; // false until New/Open — lets the toolbar/text area gate on "no document open"
	std::string m_path;           // empty until first save/open
	std::string m_text;
	std::string m_baselineText;   // text at last load/save, for dirty-tracking

	enum class PendingSwitch { None, New, OpenPath };
	PendingSwitch m_pendingSwitch = PendingSwitch::None;
	std::string   m_pendingSwitchPath;
	bool          m_openUnsavedPopup = false;

	bool m_openSaveAsPopup = false;
	char m_saveAsBuf[256] = "NewText";

	float m_fontScale = 1.0f; // Ctrl+scroll zoom over the text area, applied via ImGui::PushFont(NULL, size)
};
