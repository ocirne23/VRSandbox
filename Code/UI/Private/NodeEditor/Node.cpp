module UI.NodeEditor.Node;

using namespace NodeEditor;

Node& Node::initialize(ImVec2 initialPos, std::string name, ENodeStyle style, uint32 color)
{
    m_initialPos = initialPos;
    m_name = name;
    m_style = style;
    m_color = color;
    return *this;
}

Node& Node::initFromDef(ImVec2 initialPos, const NodeDef& def)
{
    initialize(initialPos, def.displayName, ENodeStyle_Full, 0xFF333333);
    m_typeId = def.typeId;
    for (const PinDef& pinDef : def.inputs)
    {
        addInput(pinDef.type, pinDef.name, pinDef.defaultValue);
        m_inputPins.back()->typeGroup = pinDef.typeGroup;
    }
    for (const PinDef& pinDef : def.outputs)
    {
        addOutput(pinDef.type, pinDef.name);
        m_outputPins.back()->typeGroup = pinDef.typeGroup;
    }
    return *this;
}

static EPinShape shapeForDataType(EDataType type)
{
    return type == EDataType::Exec ? EPinShape_Flow : EPinShape_Circle;
}

Node& Node::addInput(EDataType dataType, const std::string& name, const std::string& defaultValue)
{
    auto pin = std::make_unique<Pin>(name, this, EPinType_Input, shapeForDataType(dataType), dataTypeColor(dataType));
    pin->dataType = dataType;
    pin->defaultValue = defaultValue;
    m_inputPins.emplace_back(std::move(pin));
    return *this;
}

Node& Node::addOutput(EDataType dataType, const std::string& name)
{
    auto pin = std::make_unique<Pin>(name, this, EPinType_Output, shapeForDataType(dataType), dataTypeColor(dataType));
    pin->dataType = dataType;
    m_outputPins.emplace_back(std::move(pin));
    return *this;
}

namespace
{
    constexpr float kPinIcon = 16.0f;        // reserved square for a pin's shape marker
    constexpr float kColumnGap = 24.0f;      // gap between the input and output columns
    constexpr float kDefaultFieldWidth = 120.0f;

    bool hasInlineDefault(const Pin& pin)
    {
        return pin.dataType != EDataType::Exec && pin.type == EPinType_Input && pin.numConnections == 0;
    }

    // Reserves a fixed square and draws the pin's shape into it, registering it as the pin's hit-rect.
    void drawPinIcon(const Pin& pin)
    {
        const ImVec2 topLeft = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(kPinIcon, kPinIcon));
        ed::PinRect(topLeft, ImVec2(topLeft.x + kPinIcon, topLeft.y + kPinIcon));

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 c = ImVec2(topLeft.x + kPinIcon * 0.5f, topLeft.y + kPinIcon * 0.5f);
        const bool filled = pin.numConnections > 0;
        switch (pin.shape)
        {
            case EPinShape_Flow:
                drawList->AddTriangleFilled(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y), ImVec2(c.x - 5, c.y + 5), pin.color);
                break;
            case EPinShape_Circle:
                filled ? drawList->AddCircleFilled(c, 5.0f, pin.color, 12)
                       : drawList->AddCircle(c, 5.0f, pin.color, 12, 1.5f);
                break;
            case EPinShape_Square:
                filled ? drawList->AddRectFilled(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), pin.color)
                       : drawList->AddRect(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), pin.color);
                break;
            case EPinShape_None:
                break;
        }
    }

    float measureRow(const Pin& pin, float spacing)
    {
        float w = pin.shape != EPinShape_None ? kPinIcon : 0.0f;
        if (!pin.name.empty())
            w += (w > 0.0f ? spacing : 0.0f) + ImGui::CalcTextSize(pin.name.c_str()).x;
        if (hasInlineDefault(pin))
            w += spacing + kDefaultFieldWidth;
        return w;
    }

    // Input row, left aligned at leftX: [icon] name [default field]. No trailing SameLine.
    void renderInputRow(Pin& pin, float leftX, float spacing)
    {
        bool firstElement = true;
        auto sameLine = [&] { if (!firstElement) ImGui::SameLine(0.0f, spacing); firstElement = false; };

        ImGui::SetCursorPosX(leftX);
        if (pin.shape != EPinShape_None)
        {
            sameLine();
            ed::BeginPin(pin, ed::PinKind::Input);
            drawPinIcon(pin);
            ed::EndPin();
        }
        if (!pin.name.empty())
        {
            sameLine();
            ImGui::TextUnformatted(pin.name.c_str());
        }
        if (hasInlineDefault(pin))
        {
            sameLine();
            char buf[96];
            const std::string& value = pin.defaultValue;
            for (size_t k = 0; k < sizeof(buf); ++k)
                buf[k] = (k + 1 < sizeof(buf) && k < value.size()) ? value[k] : '\0';
            ImGui::PushID(&pin);
            ImGui::SetNextItemWidth(kDefaultFieldWidth);
            if (ImGui::InputText("##v", buf, sizeof(buf)))
                pin.defaultValue = buf;
            ImGui::PopID();
        }
    }

    // Output row, right aligned so its right edge lands on rightX: name [icon].
    void renderOutputRow(Pin& pin, float rightX, float spacing)
    {
        ImGui::SetCursorPosX(rightX - measureRow(pin, spacing));
        bool firstElement = true;
        auto sameLine = [&] { if (!firstElement) ImGui::SameLine(0.0f, spacing); firstElement = false; };

        if (!pin.name.empty())
        {
            sameLine();
            ImGui::TextUnformatted(pin.name.c_str());
        }
        if (pin.shape != EPinShape_None)
        {
            sameLine();
            ed::BeginPin(pin, ed::PinKind::Output);
            drawPinIcon(pin);
            ed::EndPin();
        }
    }
}

void Node::update(double /*deltaSec*/, bool firstFrame)
{
    if (firstFrame)
        ed::SetNodePosition(*this, m_initialPos);

    ed::BeginNode(*this);
    ImGui::PushID(this);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    // A single exec pin rides on the title line (input left, output right). Data pins go in the body
    // columns; so do exec outputs when there are 2+ of them (e.g. Branch's true/false) so the title
    // line stays clean.
    Pin* inputExec = nullptr;
    std::vector<Pin*> dataInputs;
    for (const auto& pinPtr : m_inputPins)
    {
        if (pinPtr->dataType == EDataType::Exec && !inputExec)
            inputExec = pinPtr.get();
        else
            dataInputs.push_back(pinPtr.get());
    }

    int execOutputCount = 0;
    for (const auto& pinPtr : m_outputPins)
        if (pinPtr->dataType == EDataType::Exec)
            ++execOutputCount;

    Pin* titleOutputExec = nullptr;
    std::vector<Pin*> bodyOutputs;
    for (const auto& pinPtr : m_outputPins)
    {
        if (execOutputCount == 1 && pinPtr->dataType == EDataType::Exec && !titleOutputExec)
            titleOutputExec = pinPtr.get();
        else
            bodyOutputs.push_back(pinPtr.get());
    }

    // Measure first so the layout is stable on the very first frame (no feedback from GetNodeSize).
    float inputColW = 0.0f;
    for (Pin* pin : dataInputs)
        inputColW = fmaxf(inputColW, measureRow(*pin, spacing));
    float outputColW = 0.0f;
    for (Pin* pin : bodyOutputs)
        outputColW = fmaxf(outputColW, measureRow(*pin, spacing));

    // Optional dropdown property (e.g. the Conditional's comparison operator), shown as a cycle button.
    const NodeDef* def = findNodeDef(m_typeId);
    const bool hasProperty = def && !def->enumOptions.empty();
    float propW = 0.0f;
    if (hasProperty)
    {
        for (const std::string& option : def->enumOptions)
            propW = fmaxf(propW, ImGui::CalcTextSize(option.c_str()).x);
        propW += ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
        if (m_enumSelection < 0 || m_enumSelection >= (int)def->enumOptions.size())
            m_enumSelection = 0;
    }

    const float titleW = ImGui::CalcTextSize(m_name.c_str()).x;
    const float leftExecW = inputExec ? (kPinIcon + spacing) : 0.0f;
    const float rightExecW = titleOutputExec ? (spacing + kPinIcon) : 0.0f;
    const float titleLineW = leftExecW + titleW + rightExecW;
    const float bodyW = inputColW + ((inputColW > 0.0f && outputColW > 0.0f) ? kColumnGap : 0.0f) + outputColW;
    const float nodeW = fmaxf(fmaxf(titleLineW, bodyW), propW);

    const ImVec2 nodeOriginScreen = ImGui::GetCursorScreenPos();
    const float nodeLeftX = ImGui::GetCursorPosX();

    // ---- title line: [exec in] title [exec out] ----
    ImGui::BeginGroup();
    ImGui::SetCursorPosX(nodeLeftX);
    if (inputExec)
    {
        ed::BeginPin(*inputExec, ed::PinKind::Input);
        drawPinIcon(*inputExec);
        ed::EndPin();
        ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::SetCursorPosX(nodeLeftX + leftExecW + fmaxf(0.0f, (nodeW - leftExecW - rightExecW - titleW) * 0.5f));
    ImGui::TextUnformatted(m_name.c_str());
    if (titleOutputExec)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(nodeLeftX + nodeW - kPinIcon);
        ed::BeginPin(*titleOutputExec, ed::PinKind::Output);
        drawPinIcon(*titleOutputExec);
        ed::EndPin();
    }
    ImGui::EndGroup();

    // Divider line beneath the title line, spanning the full node width.
    const float dividerY = ImGui::GetItemRectMax().y + 2.0f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(nodeOriginScreen.x, dividerY),
        ImVec2(nodeOriginScreen.x + nodeW, dividerY), ImColor(255, 255, 255, 40), 1.0f);
    ImGui::Dummy(ImVec2(nodeW, 4.0f)); // reserve full width + a little breathing room below the title

    if (hasProperty)
    {
        const int count = (int)def->enumOptions.size();
        ImGui::SetCursorPosX(nodeLeftX + (nodeW - propW) * 0.5f);
        if (ImGui::Button(def->enumOptions[m_enumSelection].c_str(), ImVec2(propW, 0.0f)))
            m_enumSelection = (m_enumSelection + 1) % count; // click to cycle through the operators
        ImGui::Dummy(ImVec2(nodeW, 2.0f));
    }

    const float bodyTopY = ImGui::GetCursorPosY();

    // Input column (left), then output column (right) starting back at the same Y.
    for (Pin* pin : dataInputs)
        renderInputRow(*pin, nodeLeftX, spacing);

    if (!bodyOutputs.empty())
        ImGui::SetCursorPosY(bodyTopY);
    for (Pin* pin : bodyOutputs)
        renderOutputRow(*pin, nodeLeftX + nodeW, spacing);

    ImGui::PopID();
    ed::EndNode();
}