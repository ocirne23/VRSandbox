module UI.NodeEditor.Node;

using namespace NodeEditor;

void Node::initialize(ed::NodeId id, ed::PinId entryPin, ed::PinId exitPin, ImVec2 initialPos, std::string name, const std::vector<Pin>& inputPins, const std::vector<Pin>& outputPins)
{
    m_id = id;
    m_entryPin = entryPin;
    m_exitPin = exitPin;
    m_initialPos = initialPos;
    m_name = name;
    m_inputPins = inputPins;
    m_outputPins = outputPins;
}

void Node::update(double deltaSec, bool firstFrame)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (firstFrame)
        ed::SetNodePosition(m_id, m_initialPos);

    ImGui::SetNextWindowContentSize(ImVec2(500.0f, 500.0f));
    ed::BeginNode(m_id);

    auto nodeSize = ed::GetNodeSize(m_id);
    const char* title = m_name.c_str();
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    float spacing = ImGui::GetStyle().ItemSpacing.x;


    float pinSize = ImGui::CalcTextSize("  ").x;
    {
        ed::BeginPin(m_entryPin, ed::PinKind::Input);
        ImGui::Text("  ");
        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        auto size = ImGui::GetItemRectSize();
        ImVec2 centerPos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
        ed::PinRect(min, max);
        draw_list->AddTriangleFilled(
            ImVec2(centerPos.x - 5.0f, centerPos.y + 5.0f),
            ImVec2(centerPos.x - 5.0f, centerPos.y - 5.0f),
            ImVec2(centerPos.x + 5.0f, centerPos.y), ImColor(200, 200, 200));
        ed::EndPin();
    }
    {
        auto pos = ed::GetNodePosition(m_id);
        ImGui::SameLine(fmax(nodeSize.x * 0.5f - pinSize * 2.0f - titleSize.x * 0.5f - spacing * 2.0f, 0.0f));
        //ImGui::SameLine(0.0f);
        //ImGui::SetCursorPosX(ImGui::GetCursorPosX() + );
        ImGui::Text(title);
        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        draw_list->AddLine(ImVec2(pos.x + 2.0f, max.y + 1.0f), ImVec2(pos.x + nodeSize.x - 2.0f, max.y + 1.0f), ImColor(255, 255, 255), 1.0f);
    }
    
    {
        //ImGui::SameLine(0.0f);

       // auto posX = (nodeSize.x - ImGui::GetCursorPosX() + nodeSize.x - pinSize);
       // if (posX > ImGui::GetCursorPosX())
       //     ImGui::SetCursorPosX(posX);

        ImGui::SameLine(fmax(nodeSize.x * 0.5f - pinSize * 2.0f - titleSize.x * 0.5f - spacing * 2.0f, 0.0f));

        ed::BeginPin(m_exitPin, ed::PinKind::Output);
        ImGui::Text("  ");
        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        auto size = ImGui::GetItemRectSize();
        ImVec2 centerPos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
        ed::PinRect(min, max);
        draw_list->AddTriangleFilled(
            ImVec2(centerPos.x - 5.0f, centerPos.y + 5.0f),
            ImVec2(centerPos.x - 5.0f, centerPos.y - 5.0f),
            ImVec2(centerPos.x + 5.0f, centerPos.y), ImColor(200, 200, 200));
        ed::EndPin();
    }
    
    ImGui::BeginGroup();
    for (const Pin& inputPin : m_inputPins)
    {
        ed::BeginPin(inputPin.id, ed::PinKind::Input);
        {
            ImGui::Text("  ");
            auto min = ImGui::GetItemRectMin();
            auto max = ImGui::GetItemRectMax();
            auto size = ImGui::GetItemRectSize();
            ImVec2 circlePos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
            ImGui::SameLine(size.x);
            ImGui::Text(inputPin.name.c_str());
            ed::PinRect(min, max);
            draw_list->AddCircle(circlePos, 5.0f, inputPin.color, 12);
        }
        ed::EndPin();
    }
    ImGui::EndGroup();

    auto firstColumnSize = ImGui::GetItemRectSize();
    ImGui::SameLine(0.0f);
    ImGui::BeginGroup();

    float nodeWidth = ed::GetNodeSize(m_id).x;

   // for (const Pin& outputPin : m_outputPins)
   // {
   //     ed::BeginPin(outputPin.id, ed::PinKind::Output);
   //     {
   //         const char* text = outputPin.name.c_str();
   //         float textSize = ImGui::CalcTextSize(text).x;
   //         float contentSize = spacing + textSize + pinSize + 1.0f;
   //
   //         auto posX = (ImGui::GetCursorPosX() + nodeWidth - contentSize - firstColumnSize.x - spacing * 2.0f + 8.0f);
   //         if (posX > ImGui::GetCursorPosX())
   //             ImGui::SetCursorPosX(posX);
   //
   //         //ImGui::SameLine(fmax(nodeWidth - contentSize - firstColumnSize.x - spacing + 7.0f, 0.0f), -1.0f);
   //         ImGui::Text(text);
   //         ImGui::SameLine(0.0f, 1.0f);
   //         ImGui::Text("  ");
   //         auto min = ImGui::GetItemRectMin();
   //         auto max = ImGui::GetItemRectMax();
   //         auto size = ImGui::GetItemRectSize();
   //         ImVec2 circlePos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
   //         ed::PinRect(min, max);
   //         draw_list->AddCircle(circlePos, 5.0f, outputPin.color, 12);
   //     }
   //     ed::EndPin();
   // }
    ImGui::EndGroup();

    ed::EndNode();
}