export module UI.NodeEditor.Node;

import Core;
import UI.node_editor;

namespace NodeEditor
{

export class Node
{
public:

    struct Pin
    {
        ed::PinId id;
        std::string name;
        uint32 color;
        uint32 shape;
    };

    void initialize(ed::NodeId id, ed::PinId entryPin, ed::PinId exitPin, ImVec2 initialPos, std::string name, const std::vector<Pin>& inputPins, const std::vector<Pin>& outputPins);

    void update(double deltaSec, bool firstFrame);

private:

    ed::NodeId m_id;
    ed::PinId m_entryPin;
    ed::PinId m_exitPin;
    ImVec2 m_initialPos;
    std::string m_name;
    std::vector<Pin> m_inputPins;
    std::vector<Pin> m_outputPins;
};

}

