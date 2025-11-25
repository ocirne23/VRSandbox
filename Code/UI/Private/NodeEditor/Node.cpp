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

    ed::BeginNode(m_id);

    const auto nodePos = ed::GetNodePosition(m_id);
    const auto nodeSize = ed::GetNodeSize(m_id);
    const char* title = m_name.c_str();
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float pinSize = ImGui::CalcTextSize("  ").x;

    if (firstFrame) // initialize node size
    {
        float maxInputSize = 0.0f;
        float maxOutputSize = 0.0f;
        for (Pin& inputPin : m_inputPins)
            maxInputSize = fmax(maxInputSize, ImGui::CalcTextSize(inputPin.name.c_str()).x + pinSize + 1.0f);
        for (Pin& outputPin : m_outputPins)
            maxOutputSize = fmax(maxOutputSize, ImGui::CalcTextSize(outputPin.name.c_str()).x + pinSize + 1.0f);
        const float pinsSize = maxInputSize + maxOutputSize;
        const float titlebarSize = titleSize.x + pinSize * 2 + spacing * 2;
        ImGui::SameLine(fmax(titlebarSize, pinsSize), 0.0f);
        ImGui::Dummy(ImVec2(0,0));
        ed::EndNode();
        return;
    }

    {
        ed::BeginPin(m_entryPin, ed::PinKind::Input);
        ImGui::Text("  ");
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        const auto size = ImGui::GetItemRectSize();
        ImVec2 centerPos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
        ed::PinRect(min, max);
        draw_list->AddTriangleFilled(
            ImVec2(centerPos.x - 5.0f, centerPos.y + 5.0f),
            ImVec2(centerPos.x - 5.0f, centerPos.y - 5.0f),
            ImVec2(centerPos.x + 5.0f, centerPos.y), ImColor(200, 200, 200));
        ed::EndPin();
    }
    {
        ImGui::SameLine(0.0f, -1.0f);
        const float cursorMove = nodePos.x + nodeSize.x * 0.5f - titleSize.x * 0.5f;
        if (cursorMove > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(cursorMove);
        ImGui::Text(title);
        const auto max = ImGui::GetItemRectMax();
        draw_list->AddLine(ImVec2(nodePos.x + 2.0f, max.y + 1.0f), ImVec2(nodePos.x + nodeSize.x - 2.0f, max.y + 1.0f), ImColor(255, 255, 255), 1.0f);
    }
   
    {
        ImGui::SameLine(0.0f, -1.0f);
        const float cursorMove = nodePos.x + nodeSize.x - pinSize - spacing;
        if (cursorMove > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(cursorMove);

        ed::BeginPin(m_exitPin, ed::PinKind::Output);
        ImGui::Text("  ");
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        const auto size = ImGui::GetItemRectSize();
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
            const auto min = ImGui::GetItemRectMin();
            const auto max = ImGui::GetItemRectMax();
            const auto size = ImGui::GetItemRectSize();
            const ImVec2 circlePos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
            ImGui::SameLine(0.0f, 1.0f);
            ImGui::Text(inputPin.name.c_str());
            ed::PinRect(min, max);
            draw_list->AddCircle(circlePos, 5.0f, inputPin.color, 12);
        }
        ed::EndPin();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f);
    ImGui::BeginGroup();
    for (const Pin& outputPin : m_outputPins)
    {
        ed::BeginPin(outputPin.id, ed::PinKind::Output);
        {
            const char* text = outputPin.name.c_str();
            const float cursorMove = nodePos.x + nodeSize.x - ImGui::CalcTextSize(text).x - pinSize - spacing - 1.0f;
            if (cursorMove > ImGui::GetCursorPosX())
                ImGui::SetCursorPosX(cursorMove);
            ImGui::Text(text);
            ImGui::SameLine(0.0f, 1.0f);
            ImGui::Text("  ");
            const auto min = ImGui::GetItemRectMin();
            const auto max = ImGui::GetItemRectMax();
            const auto size = ImGui::GetItemRectSize();
            const ImVec2 circlePos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
            ed::PinRect(min, max);
            draw_list->AddCircle(circlePos, 5.0f, outputPin.color, 12);
        }
        ed::EndPin();
    }
    ImGui::EndGroup();
    
    ed::EndNode();
}