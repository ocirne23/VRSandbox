export module UI.NodeEditor.Node;

import Core;
import Core.imgui;
import UI.imgui_node_editor;
import UI.NodeEditor.NodeDef;

namespace NodeEditor
{
class Node;

export enum EPinShape : uint8
{
    EPinShape_Flow,
    EPinShape_Circle,
    EPinShape_Square,
    EPinShape_None,
};

export enum EPinType : uint8
{
    EPinType_Input,
    EPinType_Output,
};

export enum ENodeStyle : uint8
{
    ENodeStyle_Minimal,      // First input & output pins will be on title line
    ENodeStyle_MinSeparator, // First input & output pins will be on title line the rest underneath with separator
    ENodeStyle_Full,         // All pins underneath title with separator
};

export struct Pin
{
    std::string name;
    Node* node = nullptr;
    EPinType type;
    EPinShape shape;
    uint32 color;
    EDataType dataType = EDataType::Exec;
    std::string defaultValue;          // C++ literal used for an unconnected data input
    int typeGroup = 0;                 // wildcard pins sharing a group resolve to the same concrete type
    EMutableType mutability = EMutableType::Readable; // input: required of its source; output: what it provides
    uint8 numConnections = 0;

    operator ed::PinId() const { return ed::PinId(this); }
};

export class Node
{
public:

	Node(uint32 nodeIdx) : m_nodeIdx(nodeIdx) {}
    Node& initialize(ImVec2 initialPos, std::string name, ENodeStyle style, uint32 color);
    Node& initFromDef(ImVec2 initialPos, const NodeDef& def);
    Node& addInput(EDataType dataType, const std::string& name, const std::string& defaultValue);
    Node& addOutput(EDataType dataType, const std::string& name);

    void update(double deltaSec, bool firstFrame);

	uint32 getNodeIdx() const { return m_nodeIdx; }
    ed::NodeId getId() const { return ed::NodeId(this); }
    operator ed::NodeId() const { return ed::NodeId(this); }

    const std::string& getTypeId() const { return m_typeId; }
    const std::vector<std::unique_ptr<Pin>>& getInputPins() const { return m_inputPins; }
    const std::vector<std::unique_ptr<Pin>>& getOutputPins() const { return m_outputPins; }

    int getEnumSelection() const { return m_enumSelection; }
    void setEnumSelection(int selection) { m_enumSelection = selection; }

private:

	uint32 m_nodeIdx;
    ImVec2 m_initialPos;
    uint32 m_color;
    ENodeStyle m_style;
    std::string m_name;
    std::string m_typeId;
    int m_enumSelection = 0;             // index into the node def's enumOptions (dropdown property)
    std::vector<std::unique_ptr<Pin>> m_inputPins;
    std::vector<std::unique_ptr<Pin>> m_outputPins;
};

} // namespace NodeEditor
