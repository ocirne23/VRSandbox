module UI.NodeEditor.Link;

using namespace NodeEditor;

void Link::initialize(ed::PinId inputId, ed::PinId outputId)
{
    m_inputId = inputId;
    m_outputId = outputId;
}

void Link::update(double deltaSec, bool firstFrame)
{
    ed::Link(ed::LinkId(this), m_inputId, m_outputId);
}