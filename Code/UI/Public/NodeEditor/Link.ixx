export module UI.NodeEditor.Link;

import UI.node_editor;

namespace NodeEditor
{

export class Link
{
public:

    void initialize(ed::PinId inputId, ed::PinId outputId);
    void update(double deltaSec, bool firstFrame);

    ed::LinkId getId() const { return ed::LinkId(this); }
    operator ed::LinkId() const { return ed::LinkId(this); }

private:

    ed::PinId m_inputId;
    ed::PinId m_outputId;
};

}

