export module UI.NodeEditor.Link;

import UI.node_editor;

namespace NodeEditor
{

export class Link
{
public:

    void initialize(ed::LinkId id, ed::PinId inputId, ed::PinId outputId);
    void update(double deltaSec, bool firstFrame);

    ed::LinkId getId() const { return m_id; }

private:

    ed::LinkId m_id;
    ed::PinId m_inputId;
    ed::PinId m_outputId;
};

}

