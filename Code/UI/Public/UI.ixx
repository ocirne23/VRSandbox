export module UI;

import Core;
import Core.Rect;

import UI.fwd;
import UI.imgui_node_editor;
import UI.NodeEditor;

export class UI final
{
public:

    UI() {}
    ~UI();
    UI(const UI&) = delete;

    void initialize();
    void update(double deltaSec);
    void render();

    bool isViewportGrabbed() const { return m_isViewportGrabbed; }
    bool isViewportFocused() const { return m_isViewportFocused; }
    bool hasViewportGainedFocused() const { return m_hasViewportGainedFocus; }
    const Rect& getViewportRect() const { return m_viewportRect; }

private:

    bool m_isViewportGrabbed = false;
    bool m_isViewportFocused = false;
    bool m_hasViewportGainedFocus = false;
    Rect m_viewportRect = Rect();

    ed::EditorContext* m_nodeEditorContext = nullptr;

    NodeEditor::Scene m_scene;
};

export namespace Globals
{
    UI ui;
}