export module UI.NodeEditor.Node;

import Core;
import UI.node_editor;

namespace NodeEditor
{

export class Scene;

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

class Node;

export struct Pin
{
    std::string name;
    Node* node = nullptr;
    EPinType type;
    EPinShape shape;
    uint32 color;
    uint8 numConnections = 0;

    operator ed::PinId() const { return ed::PinId(this); }
};

export class Node
{
public:

    Node& initialize(Scene* scene, ImVec2 initialPos, std::string name, ENodeStyle style, uint32 color);
    Node& addInputPin(EPinShape shape, const std::string& name, uint32 color);
    Node& addOutputPin(EPinShape shape, const std::string& name, uint32 color);

    void update(double deltaSec, bool firstFrame);

    ed::NodeId getId() const { return ed::NodeId(this); }
    operator ed::NodeId() const { return ed::NodeId(this); }

private:

    Scene* m_scene = nullptr;
    ImVec2 m_initialPos;
    uint32 m_color;
    ENodeStyle m_style;
    std::string m_name;
    std::vector<std::unique_ptr<Pin>> m_inputPins;
    std::vector<std::unique_ptr<Pin>> m_outputPins;
};

}

