export module UI.NodeEditor.Scene;

import Core;

import UI.node_editor;
import UI.NodeEditor.Node;
import UI.NodeEditor.Link;

namespace NodeEditor
{

export class Scene
{
public:

    ~Scene();
    void initialize();
    void update(double deltaSec);

    void processInteractions();

private:

    ed::EditorContext* m_nodeEditorContext = nullptr;

    std::vector<Node> m_nodes;
    std::vector<Link> m_links;

    bool m_firstFrame = true;
    uint64_t m_idCounter = 1;
};

}