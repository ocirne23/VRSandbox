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

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ed::PinId(m_idCounter++), ed::PinId(m_idCounter++), ImVec2(100, 100), "Node 1aaaaaaaaaaaaaaaaaa",
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), "Input A", 0xFFAA3333, 0 },
            { ed::PinId(m_idCounter++), "Input B", 0xFF33AA33, 0 }
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), "Output A", 0xFF3333AA, 1 }
    });

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ed::PinId(m_idCounter++), ed::PinId(m_idCounter++), ImVec2(400, 100), "Node 2",
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), "Input Caaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 0xFFAA3333, 0 }
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), "Output B", 0xFF33AA33, 1 },
            { ed::PinId(m_idCounter++), "Output C", 0xFF3333AA, 1 }
    });
}

void Scene::update(double deltaSec)
{
    ed::SetCurrentEditor(m_nodeEditorContext);
    ed::Begin("My Editor", ImVec2(0.0, 0.0f));
    ed::GetStyle().NodePadding = ImVec4(8, 4, 8, 8);

    for (Node& node : m_nodes)
    {
        node.update(deltaSec, m_firstFrame);
    }

    processInteractions();

    for (Link& link : m_links)
    {
        link.update(deltaSec, m_firstFrame);
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
                    m_links.emplace_back().initialize(ed::LinkId(m_idCounter++), inputPinId, outputPinId);
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
                    if (it->getId() == deletedLinkId)
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