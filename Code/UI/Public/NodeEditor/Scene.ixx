export module UI.NodeEditor.Scene;
extern "C++" {

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

    Node& createNode();

private:

    ed::EditorContext* m_nodeEditorContext = nullptr;

    std::vector<std::unique_ptr<Node>> m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;

    bool m_firstFrame = true;
    uint64_t m_idCounter = 1;
};

} // namespace NodeEditor
} // extern "C++"