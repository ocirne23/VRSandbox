export module UI;

import Core;
import Core.Rect;
import Core.glm;
import Entity;

import UI.fwd;
import UI.imgui_node_editor;
import UI.NodeEditor;
import UI.AssetBrowser;
import UI.SceneView;
import UI.PropertiesPanel;
import UI.OutputLog;
import UI.TweakPanel;

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
        return changes;
    }

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

private:

    bool m_isViewportGrabbed = false;
    bool m_isViewportFocused = false;
    bool m_hasViewportGainedFocus = false;
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
	OutputLog       m_outputLog;
	TweakPanel      m_tweakPanel;
};

export namespace Globals
{
    UI ui;
}
