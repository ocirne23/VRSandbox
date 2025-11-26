module UI.NodeEditor.Node;

using namespace NodeEditor;

Node& Node::initialize(Scene* scene, ImVec2 initialPos, std::string name, ENodeStyle style, uint32 color)
{
    m_scene = scene;
    m_initialPos = initialPos;
    m_name = name;
    m_style = style;
    m_color = color;
    return *this;
}

Node& Node::addInputPin(EPinShape shape, const std::string& name, uint32 color)
{
    m_inputPins.emplace_back(std::make_unique<Pin>(name, this, EPinType_Input, shape, color));
    return *this;
}

Node& Node::addOutputPin(EPinShape shape, const std::string& name, uint32 color)
{
    m_outputPins.emplace_back(std::make_unique<Pin>(name, this, EPinType_Output, shape, color));
    return *this;
}

static float getPinSize(const Pin& pin)
{
    return ImGui::CalcTextSize(pin.name.c_str()).x + (pin.shape != EPinShape_None ? ImGui::CalcTextSize("  ").x + 1.0f : 0.0f);
}

static void drawPin(const Pin& pin)
{
    auto shape = pin.shape;
    if (shape == EPinShape_None)
        return;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::Text("  ");
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    const auto size = ImGui::GetItemRectSize();
    ed::PinRect(min, max);
    if (shape == EPinShape_Flow)
    {
        ImVec2 centerPos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
        draw_list->AddTriangleFilled(
            ImVec2(centerPos.x - 5.0f, centerPos.y + 5.0f),
            ImVec2(centerPos.x - 5.0f, centerPos.y - 5.0f),
            ImVec2(centerPos.x + 5.0f, centerPos.y), pin.color);
    }
    else if (shape == EPinShape_Circle)
    {
        ImVec2 centerPos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
        draw_list->AddCircle(centerPos, 5.0f, pin.color, 12);
    }
    else if (shape == EPinShape_Square)
    {
        ImVec2 centerPos = ImVec2(min.x + size.x / 2.0f, min.y + size.y / 2.0f);
        draw_list->AddRect(ImVec2(centerPos.x - 5.0f, centerPos.y - 5.0f), ImVec2(centerPos.x + 5.0f, centerPos.y + 5.0f), pin.color);
    }
    else
    {
        assert(false);
    }
}

void Node::update(double deltaSec, bool firstFrame)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (firstFrame)
        ed::SetNodePosition(*this, m_initialPos);

    ed::BeginNode(*this);

    const auto nodePos = ed::GetNodePosition(*this);
    const auto nodeSize = ed::GetNodeSize(*this);
    const char* title = m_name.c_str();
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    if (firstFrame) // initialize node size
    {
        float firstInputPinSize = m_inputPins.empty() ? 0.0f : getPinSize(*m_inputPins[0]);
        float firstOutputPinSize = m_outputPins.empty() ? 0.0f : getPinSize(*m_outputPins[0]);

        float maxInputSize = m_style == ENodeStyle_Full ? firstInputPinSize : 0.0f;
        float maxOutputSize = m_style == ENodeStyle_Full ? firstOutputPinSize : 0.0f;
        for (uint32 i = 1; i < m_inputPins.size(); ++i)
        {
            maxInputSize = getPinSize(*m_inputPins[i]);
        }
        for (uint32 i = 1; i < m_outputPins.size(); ++i)
        {
            maxOutputSize = getPinSize(*m_outputPins[i]);
        }
        float titlebarSize = titleSize.x;
        if (m_style != ENodeStyle_Full)
        {
            titlebarSize += firstInputPinSize + firstOutputPinSize;
        }
        float size = fmax(titlebarSize, maxInputSize + maxOutputSize);
        ImGui::SameLine(size, 0.0f);
        ImGui::Dummy(ImVec2(0,0));
        ed::EndNode();
        return;
    }

    // Draw first input pin
    if (m_style != ENodeStyle_Full && !m_inputPins.empty())
    {
        const Pin& inputPin = *m_inputPins[0];
        if (inputPin.shape != EPinShape_None)
        {
            ed::BeginPin(inputPin, ed::PinKind::Input);
            drawPin(inputPin);
            ed::EndPin();
        }
        if (!inputPin.name.empty())
        {
            if (inputPin.shape != EPinShape_None)
                ImGui::SameLine(0.0f, 1.0f);
            ImGui::Text(inputPin.name.c_str());
        }
        ImGui::SameLine(0.0f, -1.0f);
    }
    // Draw title
    {
        float align = 0.5f;
        if (m_style != ENodeStyle_Full)
        {
            if (m_inputPins.empty() && !m_outputPins.empty())
                align = 0.0f;
            else if (!m_inputPins.empty() && m_outputPins.empty())
                align = 0.5f;
        }
        const float cursorMove = nodePos.x + nodeSize.x * align - titleSize.x * align;
        if (cursorMove > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(cursorMove);
        ImGui::Text(title);
        if (m_style != ENodeStyle_Minimal)
        {
            const auto max = ImGui::GetItemRectMax();
            draw_list->AddLine(ImVec2(nodePos.x + 2.0f, max.y + 1.0f), ImVec2(nodePos.x + nodeSize.x - 2.0f, max.y + 1.0f), ImColor(255, 255, 255), 1.0f);
        }
    }
    // Draw first output pin
    if (m_style != ENodeStyle_Full && !m_outputPins.empty())
    {
        const Pin& outputPin = *m_outputPins[0];
        ImGui::SameLine(0.0f, -1.0f);

        const float cursorMove = nodePos.x + nodeSize.x - getPinSize(outputPin) - spacing;
        if (cursorMove > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(cursorMove);

        if (!outputPin.name.empty())
        {
            ImGui::Text(outputPin.name.c_str());
        }

        if (outputPin.shape != EPinShape_None)
        {
            if (!outputPin.name.empty())
                ImGui::SameLine(0.0f, 1.0f);
            ed::BeginPin(outputPin, ed::PinKind::Output);
            drawPin(outputPin);
            ed::EndPin();
        }
    }

    bool hasInputGroup = false;    
    for (int i = (m_style != ENodeStyle_Full ? 1 : 0); i < m_inputPins.size(); ++i)
    {
        if (!hasInputGroup)
        {
            ImGui::BeginGroup();
            hasInputGroup = true;
        }
        const Pin& inputPin = *m_inputPins[i];
        if (inputPin.shape != EPinShape_None)
        {
            ed::BeginPin(inputPin, ed::PinKind::Input);
            drawPin(inputPin);
            ed::EndPin();
        }
        if (!inputPin.name.empty())
        {
            if (inputPin.shape != EPinShape_None)
                ImGui::SameLine(0.0f, 1.0f);
            ImGui::Text(inputPin.name.c_str());
        }
    }

    bool hasOutputGroup = false;
    for (int i = (m_style != ENodeStyle_Full ? 1 : 0); i < m_outputPins.size(); ++i)
    {
        if (hasInputGroup)
        {
            ImGui::EndGroup();
            ImGui::SameLine(0.0f);
            hasInputGroup = false;
        }
        if (!hasOutputGroup)
        {
            ImGui::BeginGroup();
            hasOutputGroup = true;
        }
        const Pin& outputPin = *m_outputPins[i];
        const float cursorMove = nodePos.x + nodeSize.x - getPinSize(outputPin) - spacing - 1.0f;
        if (cursorMove > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(cursorMove);

        if (!outputPin.name.empty())
        {
            ImGui::Text(outputPin.name.c_str());
        }
        if (outputPin.shape != EPinShape_None)
        {
            if (!outputPin.name.empty())
                ImGui::SameLine(0.0f, 1.0f);
            ed::BeginPin(outputPin, ed::PinKind::Output);
            drawPin(outputPin);
            ed::EndPin();
        }
    }
    if (hasOutputGroup || hasInputGroup)
    {
        ImGui::EndGroup();
    }
    
    ed::EndNode();
}