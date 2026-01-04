module UI.NodeEditor:Scene;

import :Node;
import :Link;

using namespace NodeEditor;

Scene::~Scene()
{
}

void Scene::initialize()
{
    ed::Config config;
    config.SettingsFile = nullptr;
    m_nodeEditorContext = ed::CreateEditor(&config);

    // Layout testing...
    
    createNode().initialize(this, ImVec2(100, 100), "Node 1", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "Input B", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

    createNode().initialize(this, ImVec2(100, 200), "Node 2", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255));

    createNode().initialize(this, ImVec2(100, 300), "Node 3", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output C", ImColor(0, 0, 255));

    createNode().initialize(this, ImVec2(100, 400), "Node 4", ENodeStyle_Minimal, 0xFF444444)
        .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "Input Aaaaaaaaaaaaaaaaaaaaaaaa", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

    createNode().initialize(this, ImVec2(100, 500), "Node 5", ENodeStyle_Minimal, 0xFF444444)
        .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

    createNode().initialize(this, ImVec2(500, 100), "Node 1", ENodeStyle_Full, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "Input B", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
        .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

     createNode().initialize(this, ImVec2(500, 200), "Node 2", ENodeStyle_Full, 0xFF444444)
        .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255));

     createNode().initialize(this, ImVec2(500, 300), "Node 3", ENodeStyle_Full, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output C", ImColor(0, 0, 255));

     createNode().initialize(this, ImVec2(500, 400), "Node 4", ENodeStyle_Full, 0xFF444444)
         .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addInputPin(EPinShape_Circle, "Input Aaaaaaaaaaaaaaaaaaaaaaaa", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

     createNode().initialize(this, ImVec2(500, 500), "Node 5", ENodeStyle_Full, 0xFF444444)
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

     createNode().initialize(this, ImVec2(900, 100), "Node 1", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addInputPin(EPinShape_Circle, "Input B", ImColor(255, 0, 0))
         .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));

     createNode().initialize(this, ImVec2(900, 200), "Node 2", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255));

     createNode().initialize(this, ImVec2(900, 300), "Node 3", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "Input A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Flow, "Output A", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output C", ImColor(0, 0, 255));

     createNode().initialize(this, ImVec2(900, 400), "Node 4", ENodeStyle_MinSeparator, 0xFF444444)
         .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addInputPin(EPinShape_Circle, "Input Aaaaaaaaaaaaaaaaaaaaaaaa", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

     createNode().initialize(this, ImVec2(900, 500), "Node 5", ENodeStyle_MinSeparator, 0xFF444444)
         .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
         .addOutputPin(EPinShape_Circle, "Output A", ImColor(0, 255, 0))
         .addOutputPin(EPinShape_Circle, "Output B", ImColor(0, 0, 255));
         
    createNode().initialize(this, ImVec2(0, 100), "IF", ENodeStyle_MinSeparator, 0)
        .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_None, "", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Flow, "true", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Flow, "false", ImColor(255, 255, 255));

    createNode().initialize(this, ImVec2(0, 200), "FOR", ENodeStyle_MinSeparator, 0)
        .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Square, "list", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Flow, "DONE", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Flow, "EACH", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Circle, "index", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Circle, "item", ImColor(255, 0, 0));

    createNode().initialize(this, ImVec2(0, 300), "ENTRY", ENodeStyle_MinSeparator, 0)
        .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addOutputPin(EPinShape_Square, "list", ImColor(255, 0, 0));

    createNode().initialize(this, ImVec2(0, 400), "SET", ENodeStyle_MinSeparator, 0)
        .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "var", ImColor(255, 0, 0))
        .addInputPin(EPinShape_Circle, "value", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

    createNode().initialize(this, ImVec2(0, 500), "ADD", ENodeStyle_MinSeparator, 0)
        .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "var", ImColor(255, 0, 0))
        .addInputPin(EPinShape_Circle, "value", ImColor(255, 0, 0))
        .addOutputPin(EPinShape_Flow, "", ImColor(255, 255, 255));

    createNode().initialize(this, ImVec2(0, 600), "RETURN", ENodeStyle_MinSeparator, 0)
        .addInputPin(EPinShape_Flow, "", ImColor(255, 255, 255))
        .addInputPin(EPinShape_Circle, "sum", ImColor(255, 0, 0));

    createNode().initialize(this, ImVec2(0, 700), "VAR", ENodeStyle_Full, 0)
        .addOutputPin(EPinShape_Circle, "sum", ImColor(0, 255, 0));
}

Node& Scene::createNode()
{
    return *m_nodes.emplace_back(std::make_unique<Node>());
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
        ed::PinId pin1Id, pin2Id;
        if (ed::QueryNewLink(&pin1Id, &pin2Id))
        {
            if (pin1Id && pin2Id && pin1Id != pin2Id)
            {
                Pin* pin1 = pin1Id.AsPointer<Pin>();
                Pin* pin2 = pin2Id.AsPointer<Pin>();
                if (pin1->type == EPinType_Output)
                    std::swap(pin1, pin2);

                if (pin1->numConnections == 0 && pin1->type == EPinType_Input && pin2->type == EPinType_Output)
                {
                    if (ed::AcceptNewItem())
                    {
                        pin1->numConnections++;
                        pin2->numConnections++;
                        m_links.emplace_back(std::make_unique<Link>())->initialize(pin1Id, pin2Id);
                    }
                }
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete())
    {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId))
        {
            if (ed::AcceptDeletedItem())
            {
                for (auto it = m_links.begin(); it != m_links.end(); ++it)
                {
                    auto& link = *it;
                    if (link->getId() == deletedLinkId)
                    {
                        Pin* pin1 = link->getInputId().AsPointer<Pin>();
                        Pin* pin2 = link->getOutputId().AsPointer<Pin>();
                        pin1->numConnections--;
                        pin2->numConnections--;
                        m_links.erase(it);
                        break;
                    }
                }
            }
        }
    }
    ed::EndDelete();

}