module UI.NodeEditor.Link;

using namespace NodeEditor;

void Link::initialize(ed::LinkId id, ed::PinId inputId, ed::PinId outputId)
{
    m_id = id;
    m_inputId = inputId;
    m_outputId = outputId;
}

void Link::update(double deltaSec, bool firstFrame)
{
    ed::Link(m_id, m_inputId, m_outputId);
}