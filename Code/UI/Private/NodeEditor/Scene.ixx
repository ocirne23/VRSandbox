export module UI.NodeEditor.Scene;

import Core;

import UI.imgui_node_editor;
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
    Node& addNodeOfType(const std::string& typeId, ImVec2 pos);

    void newGraph();                                  // reset to a minimal default graph
    bool loadFromFile(const std::string& path);       // rebuild graph from the //@graph metadata
    bool saveToFile(const std::string& path);         // write generated source (code + metadata)
    std::string generateCpp();                        // node graph -> C++ source

    const std::string& scriptPath() const { return m_scriptPath; }
    void setScriptPath(const std::string& path) { m_scriptPath = path; }
    bool open(const std::string& path) { setScriptPath(path); return loadFromFile(path); } // load + make current
    bool save() { return saveToFile(m_scriptPath); }                                       // write the current file

private:

    Node* findEntry() const;
    void connectNodes(Node* from, int outIdx, Node* to, int inIdx);
    void resolveNodeTypes(Node* node); // propagate concrete types through the node's wildcard pin groups
    void removeNode(ed::NodeId nodeId);
    int  indexOfNode(const Node* node) const;
    std::string serializeGraph();

    ed::EditorContext* m_nodeEditorContext = nullptr;

    std::vector<std::unique_ptr<Node>> m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;

    bool m_firstFrame = true;
    uint64_t m_idCounter = 1;
    std::string m_scriptPath = "Scripts/Graph.scr";
    ImVec2 m_pendingAddPos = ImVec2(0.0f, 0.0f);
};

} // namespace NodeEditor
