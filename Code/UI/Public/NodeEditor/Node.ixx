export module UI.NodeEditor.Node;

import Core;
import UI.node_editor;

namespace NodeEditor
{

export class Node
{
public:

    enum EPinShape
    {
        EPinShape_Flow,
        EPinShape_Circle,
        EPinShape_Square,
        EPinShape_None,
    };

    struct Pin
    {
        ed::PinId id;
        EPinShape shape;
        std::string name;
        uint32 color;
    };

    enum ENodeStyle
    {
        ENodeStyle_Minimal, // First input & output pins will be on title line 
        ENodeStyle_Full,    // All pins underneath title with separator  
    };

    void initialize(ed::NodeId id, ImVec2 initialPos, std::string name, ENodeStyle style, uint32 color, const std::vector<Pin>& inputPins, const std::vector<Pin>& outputPins);

    void update(double deltaSec, bool firstFrame);

private:

    ed::NodeId m_id;
    ImVec2 m_initialPos;
    ENodeStyle m_style;
    uint32 m_color;
    std::string m_name;
    std::vector<Pin> m_inputPins;
    std::vector<Pin> m_outputPins;
};

}

