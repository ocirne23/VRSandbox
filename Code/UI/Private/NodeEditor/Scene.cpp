module UI.NodeEditor.Scene;

using namespace NodeEditor;

Scene::~Scene()
{

}

void Scene::initialize()
{
    ed::Config config;
    config.SettingsFile = nullptr;
    m_nodeEditorContext = ed::CreateEditor(&config);

    m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(100, 100), "Node 1", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "Input B", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

    m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(100, 200), "Node 2", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255));

    m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(100, 300), "Node 3", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output C", ImColor(0, 0, 255));

    m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(100, 400), "Node 4", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "Input Aaaaaaaaaaaaaaaaaaaaaaaa", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

    m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(100, 500), "Node 5", ENodeStyle_Minimal, 0xFF444444)
        .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

    m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(500, 100), "Node 1", ENodeStyle_Full, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "Input B", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(500, 200), "Node 2", ENodeStyle_Full, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(500, 300), "Node 3", ENodeStyle_Full, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output C", ImColor(0, 0, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(500, 400), "Node 4", ENodeStyle_Full, 0xFF444444)
         .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addInputPin(EPinShape_Circle, "Input Aaaaaaaaaaaaaaaaaaaaaaaa", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(500, 500), "Node 5", ENodeStyle_Full, 0xFF444444)
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(900, 100), "Node 1", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addInputPin(EPinShape_Circle, "Input B", ImColor(255, 0, 0))
         .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(900, 200), "Node 2", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(900, 300), "Node 3", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output C", ImColor(0, 0, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(900, 400), "Node 4", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addInputPin(EPinShape_Circle, "Input Aaaaaaaaaaaaaaaaaaaaaaaa", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

     m_nodes.emplace_back(std::make_unique<Node>())->initialize(this, ImVec2(900, 500), "Node 5", ENodeStyle_MinSeparator, 0xFF444444)
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));
}

void Scene::update(double deltaSec)
{
    ed::SetCurrentEditor(m_nodeEditorContext);
    ed::Begin("My Editor", ImVec2(0.0, 0.0f));
    ed::GetStyle().NodePadding = ImVec4(8, 8, 8, 8);

    for (auto& node : m_nodes)
    {
        node->update(deltaSec, m_firstFrame);
    }

    processInteractions();

    for (auto& link : m_links)
    {
        link->update(deltaSec, m_firstFrame);
    }

    if (m_firstFrame)
        ed::NavigateToContent(0.0f);

    ed::End();

    m_firstFrame = false;
}

void Scene::processInteractions()
{
    if (ed::BeginCreate())
    {
        ed::PinId inputPinId, outputPinId;
        if (ed::QueryNewLink(&inputPinId, &outputPinId))
        {
            if (inputPinId && outputPinId) // both are valid, let's accept link
            {
                if (ed::AcceptNewItem())
                {
                    m_links.emplace_back(std::make_unique<Link>())->initialize(inputPinId, outputPinId);
                }
            }
        }
    }
    ed::EndCreate(); // Wraps up object creation action handling.

    if (ed::BeginDelete())
    {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId))
        {
            if (ed::AcceptDeletedItem())
            {
                for (auto it = m_links.begin(); it != m_links.end(); ++it)
                {
                    if ((*it)->getId() == deletedLinkId)
                    {
                        m_links.erase(it);
                        break;
                    }
                }
            }
        }
    }
    ed::EndDelete();

}