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
    
    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(100, 100), "Node 1aaaaaaaaaaaaaaaaaa", Node::ENodeStyle_Minimal, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "Input A", 0xFFAA3333 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Input B", 0xFF33AA33 }
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Output A", 0xFF3333AA },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Output B", 0xFF3333AA }
        }
    );

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(100, 200), "Node 2", Node::ENodeStyle_Minimal, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        },
        std::vector<Node::Pin>{}
    );


    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(100, 300), "Node 3", Node::ENodeStyle_Minimal, 0xFF444444,
        std::vector<Node::Pin>{},
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        }
    );
    
    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(100, 400), "Node 4", Node::ENodeStyle_Minimal, 0xFF444444,
        std::vector<Node::Pin>{},
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "", 0xFFAA3333 },
        }
    );

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(100, 500), "Node 5", Node::ENodeStyle_Minimal, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "", 0xFFAA3333 },
        },
        std::vector<Node::Pin>{}
    );

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(100, 600), "Node 6", Node::ENodeStyle_Minimal, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        }
    );
    
    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(100, 700), "Node 7", Node::ENodeStyle_Minimal, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "Input Caaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 0xFFAA3333 }
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "Output B", 0xFF33AA33 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Output C", 0xFF3333AA }
        }
    );
    //////////////////////////////////////////////////////////////////

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(500, 100), "Node 1aaaaaaaaaaaaaaaaaa", Node::ENodeStyle_Full, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "Input A", 0xFFAA3333 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Input B", 0xFF33AA33 }
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Output A", 0xFF3333AA },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Output B", 0xFF3333AA }
        }
    );

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(500, 200), "Node 2", Node::ENodeStyle_Full, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        },
        std::vector<Node::Pin>{}
    );


    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(500, 300), "Node 3", Node::ENodeStyle_Full, 0xFF444444,
        std::vector<Node::Pin>{},
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        }
    );
    
    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(500, 400), "Node 4", Node::ENodeStyle_Full, 0xFF444444,
        std::vector<Node::Pin>{},
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "", 0xFFAA3333 },
        }
    );

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(500, 500), "Node 5", Node::ENodeStyle_Full, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "", 0xFFAA3333 },
        },
        std::vector<Node::Pin>{}
    );

    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(500, 600), "Node 6", Node::ENodeStyle_Full, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "", 0xFFAA3333 },
        }
    );
    
    m_nodes.emplace_back().initialize(ed::NodeId(m_idCounter++), ImVec2(500, 700), "Node 7", Node::ENodeStyle_Full, 0xFF444444,
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "Input Caaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 0xFFAA3333 }
        },
        std::vector<Node::Pin>{
            { ed::PinId(m_idCounter++), Node::EPinShape_Flow, "Output B", 0xFF33AA33 },
            { ed::PinId(m_idCounter++), Node::EPinShape_Circle, "Output C", 0xFF3333AA }
        }
    );
}

void Scene::update(double deltaSec)
{
    ed::SetCurrentEditor(m_nodeEditorContext);
    ed::Begin("My Editor", ImVec2(0.0, 0.0f));
    ed::GetStyle().NodePadding = ImVec4(8, 8, 8, 8);

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