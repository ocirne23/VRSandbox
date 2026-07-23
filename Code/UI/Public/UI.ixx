export module UI;

import Core;
import Core.Rect;
import Core.glm;
import Core.SDL;
import Entity;

import UI.fwd;
import :imgui_node_editor;
import :NodeDef;
import :Node;
import :Link;
import :Scene;
import :AssetBrowser;
import :SceneView;
import :PropertiesPanel;
import :EntityEditor;
import :OutputLog;
import :TweakPanel;
import :TextEditor;
import Script;
import :ScriptEditor;

import RendererVK;

export class UI final
{
public:

    UI() {}
    ~UI();
    UI(const UI&) = delete;

    void initialize();
    void update(const std::vector<EntityPtr>& rootEntities, double deltaSec);
    void render();
	void setRenderStats(const Stats& stats) { m_renderStats = stats; }

    Entity* getSelectedEntity() const { return m_sceneView.getSelected(); }

    bool isViewportGrabbed() const { return m_isViewportGrabbed; }
    bool isViewportFocused() const { return m_isViewportFocused; }
    // True while the Script Editor panel (or any child of it) holds ImGui focus -- see ScriptEditor::hasFocus.
    // Input.cpp's global-shortcut gate checks this the same way it already checks WantCaptureKeyboard/WantTextInput.
    bool isScriptEditorFocused() const { return m_scriptEditorOpen && m_scriptEditor.hasFocus(); }
    bool hasViewportGainedFocused() const { return m_hasViewportGainedFocus; }
    const Rect& getViewportRect() const { return m_viewportRect; }

    std::vector<EntityChange> takeEntityChanges()
    {
        std::vector<EntityChange> changes = m_sceneView.takeChanges();
        changes.insert(changes.end(), std::make_move_iterator(m_viewportChanges.begin()),
                                      std::make_move_iterator(m_viewportChanges.end()));
        m_viewportChanges.clear();

        std::vector<EntityChange> assetChanges = m_assetBrowser.takeChanges();
        changes.insert(changes.end(), std::make_move_iterator(assetChanges.begin()),
                                      std::make_move_iterator(assetChanges.end()));

        std::vector<EntityChange> entityEditorChanges = m_entityEditor.takeChanges();
        changes.insert(changes.end(), std::make_move_iterator(entityEditorChanges.begin()),
                                      std::make_move_iterator(entityEditorChanges.end()));
        return changes;
    }

    // Forwards the entity main.cpp just spawned/respawned for an EntityEditor request into the panel.
    void onOpened(EntityPtr root, const std::string& path) { m_entityEditor.onOpened(root, path); }
    void onEntityRespawned(EntityPtr oldEntity, EntityPtr newEntity) { m_entityEditor.onRespawned(oldEntity, newEntity); }

    // Script paths the Script panel asked the host to (re)compile + hot-reload this frame.
    std::vector<std::string> takeScriptReloadRequests()
    {
        std::vector<std::string> requests = std::move(m_scriptReloadRequests);
        m_scriptReloadRequests.clear();
        return requests;
    }

    // Triggers the Script editor's node copy/paste from outside the UI frame (e.g. a global keyboard hook in
    // main.cpp), mirroring the in-editor Ctrl+C/Ctrl+V shortcut. No-ops while an ImGui text field is being
    // edited elsewhere, so it doesn't hijack that field's own copy/paste.
    void copyScriptSelection();
    void pasteScriptSelection();

    void handleKeyEvent(SDL_Event evt);

private:

    bool m_isViewportGrabbed = false;
    bool m_isViewportFocused = false;
    bool m_hasViewportGainedFocus = false;
    bool m_scriptEditorOpen = false;
    Rect m_viewportRect = Rect();
    std::vector<EntityChange> m_viewportChanges;   // assets dropped onto the viewport, drained via takeEntityChanges
    std::vector<std::string> m_scriptReloadRequests; // Script panel compile requests, drained via takeScriptReloadRequests

    // Follow the hierarchy selection into the Script editor: when a selected entity has a ScriptComponent,
    // open its script. m_pendingScriptOpen holds a switch deferred behind the unsaved-changes prompt.
    void requestOpenScript(const std::string& path);
    Entity*     m_scriptSelectionTracked = nullptr; // last selection we reacted to (only act on changes)
    std::string m_pendingScriptOpen;                // script to open once the user resolves unsaved changes
    bool        m_openUnsavedScriptPopup = false;    // request to open the modal next frame

    ed::EditorContext* m_nodeEditorContext = nullptr;

	NodeEditor::Scene m_scene;
	Stats m_renderStats;
	AssetBrowser    m_assetBrowser;
	SceneView       m_sceneView;
	PropertiesPanel m_propertiesPanel;
	EntityEditor    m_entityEditor;
	OutputLog       m_outputLog;
	TweakPanel      m_tweakPanel;
	TextEditor      m_textEditor;
	ScriptEditor    m_scriptEditor;
};

export namespace Globals
{
    UI ui;
}
