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

    bool isDirty(); // graph changed since it was last loaded/saved (nodes/links/pins/positions differ)

private:

    Node* findEntry() const;
    Node* findScriptData() const;                     // the Script Data node (persistent members), if any
    void connectNodes(Node* from, int outIdx, Node* to, int inIdx);
    void resolveNodeTypes(Node* node); // propagate concrete types through the node's wildcard pin groups
    void removeNode(ed::NodeId nodeId);
    void removeMemberPin(Node* node, int index);      // drop a Script Data member + its links
    void pruneIncompatibleLinks(Node* node);          // drop links a member type-change made invalid
    int  indexOfNode(const Node* node) const;
    std::string serializeGraph();

    ed::EditorContext* m_nodeEditorContext = nullptr;

    std::vector<std::unique_ptr<Node>> m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;

    bool m_firstFrame = true;
    uint64_t m_idCounter = 1;
    std::string m_scriptPath = "Scripts/Graph.scr";
    ImVec2 m_pendingAddPos = ImVec2(0.0f, 0.0f);

    // Dirty tracking: the serialized graph captured right after a load/save, compared against the live
    // graph to detect unsaved edits. Captured lazily (once the graph has rendered a frame so node
    // positions are valid) — m_hasBaseline gates that.
    std::string m_baselineState;
    bool m_hasBaseline = false;
};

} // namespace NodeEditor
