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
    void update(World& world,double deltaSec);
    void render();
	void setRenderStats(const Renderer::Stats& stats) { m_renderStats = stats; }

    bool isViewportGrabbed() const { return m_isViewportGrabbed; }
    bool isViewportFocused() const { return m_isViewportFocused; }
    bool hasViewportGainedFocused() const { return m_hasViewportGainedFocus; }
    const Rect& getViewportRect() const { return m_viewportRect; }

    // An asset file dropped onto the Viewport from the asset browser. screenPos is the drop point
    // in screen pixels (same space as getViewportRect()), for the app to unproject into the world.
    struct AssetDrop
    {
        std::string path;
        glm::vec2   screenPos;
    };
    // Returns and clears the drops queued since the last call (drain once per frame).
    std::vector<AssetDrop> takeAssetDrops() { return std::move(m_assetDrops); }

    // Entities deleted in the Scene panel since the last call (drain once per frame). Each handle
    // keeps its entity alive until dropped, so the app can match pointers and release its own handle.
    std::vector<EntityPtr> takeDeletedEntities() { return m_sceneView.takeDeletedEntities(); }

    // Entities whose root-status changed via a Scene-panel reparent since the last call (drain once
    // per frame). Each {isRoot, handle}: isRoot == true means it became a top-level root and the app
    // should take the handle; isRoot == false means it was put under another entity that now owns it,
    // so the app should drop its handle (matched by pointer).
    std::vector<std::tuple<bool, EntityPtr>> handleReparentedEntities() { return m_sceneView.handleReparentedEntities(); }

private:

    bool m_isViewportGrabbed = false;
    bool m_isViewportFocused = false;
    bool m_hasViewportGainedFocus = false;
    Rect m_viewportRect = Rect();
    std::vector<AssetDrop> m_assetDrops;

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