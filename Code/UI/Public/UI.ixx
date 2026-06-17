export module UI;

import Core;
import Core.Rect;
import Core.glm;
import Entity;
import Entity.World;

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
	void setRenderStats(const Renderer::Stats& stats) { m_renderStats = stats; }

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

private:

    bool m_isViewportGrabbed = false;
    bool m_isViewportFocused = false;
    bool m_hasViewportGainedFocus = false;
    Rect m_viewportRect = Rect();
    std::vector<EntityChange> m_viewportChanges;   // assets dropped onto the viewport, drained via takeEntityChanges

    ed::EditorContext* m_nodeEditorContext = nullptr;

	NodeEditor::Scene m_scene;
	Renderer::Stats m_renderStats;
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