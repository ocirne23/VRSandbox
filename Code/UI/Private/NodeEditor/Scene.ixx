export module UI.NodeEditor.Scene;

import Core;

import UI.imgui_node_editor;
import UI.NodeEditor.Node;
import UI.NodeEditor.Link;
import UI.NodeEditor.NodeDef;

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

    Node* findEntry(const char* nodeName) const;
    Node* findScriptData() const;                     // the Script Data node (persistent members), if any
    Node* findEventEntry() const;                      // the On Event node (named entries), if any
    void connectNodes(Node* from, int outIdx, Node* to, int inIdx);
    void resolveNodeTypes(Node* node); // propagate concrete types through the node's wildcard pin groups
    void removeNode(ed::NodeId nodeId);
    void insertReroute(Link* link, ImVec2 canvasPos); // split a link with a draggable reroute waypoint
    void deleteRerouteChain(Link* link);              // delete a whole routed connection from one of its links
    void removeMemberPin(Node* node, int index);      // drop a Script Data member + its links
    void pruneIncompatibleLinks(Node* node);          // drop links a member type-change made invalid
    void applyMemberEdit(const MemberEdit& edit);     // replay a member edit across all Script Data nodes
    void syncNewMemberNode(Node& newNode);            // seed a new Script Data node from the shared member set
    void applyEventEdit(const MemberEdit& edit);      // replay an entry edit across all On Event nodes
    void syncNewEventNode(Node& newNode);             // seed a new On Event node from the shared entry set

    // ---- functions (imported reusable subgraphs) ----
    using PinSig = std::vector<std::pair<EDataType, std::string>>; // ordered (type, name) list

    struct FunctionRef
    {
        std::string scriptPath;   // .scr the function is defined in (relative, e.g. "Scripts/Math.scr")
        std::string funcName;     // the function's name (its Function Input/Output pair share it)
        std::string displayLabel; // "<file>: <func>" shown in the import menu
    };

    void applyFunctionEdit(Node* node, const MemberEdit& edit); // add/remove/rename/retype one param/return
    void removeInputPin(Node* node, int index);                 // drop an input pin + its links
    bool readFunctionSignature(const std::string& path, const std::string& funcName,
                               PinSig& params, PinSig& returns) const; // read a function's I/O from its file
    std::vector<FunctionRef> scanImportableFunctions() const;   // every function defined in Assets/Scripts/*.scr
    Node& importFunction(const FunctionRef& ref, ImVec2 pos);   // create a Function Call node for a function
    void emitFunctions(std::string& code);                      // emit C++ defs for every function used/defined here
    bool isFunctionScript() const;                              // true if this graph defines a function (Function Input)

    int  indexOfNode(const Node* node) const;
    std::string serializeGraph();

    ed::EditorContext* m_nodeEditorContext = nullptr;

    std::vector<std::unique_ptr<Node>> m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;

    bool m_firstFrame = true;
    uint64_t m_idCounter = 1;
    std::string m_scriptPath = "Scripts/Graph.scr";
    ImVec2 m_pendingAddPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_pendingAddScreenPos = ImVec2(0.0f, 0.0f); // screen-space click pos, so the popup can offset from it

    // Dirty tracking: the serialized graph captured right after a load/save, compared against the live
    // graph to detect unsaved edits. Captured lazily (once the graph has rendered a frame so node
    // positions are valid) — m_hasBaseline gates that.
    std::string m_baselineState;
    bool m_hasBaseline = false;

    std::vector<FunctionRef> m_importFunctions; // cached function list, refreshed when the add-node popup opens
};

} // namespace NodeEditor
