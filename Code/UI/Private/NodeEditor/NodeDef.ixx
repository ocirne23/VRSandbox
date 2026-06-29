export module UI.NodeEditor.NodeDef;

import Core;

namespace NodeEditor
{

// Data carried on a pin / connection. Exec is control flow; the rest are value types.
// Wildcard is an unresolved generic pin that adopts the concrete type it gets connected to.
export enum class EDataType : uint8
{
    Exec,
    Bool,
    Int,
    Float,
    Vec3,
    String,
    Wildcard,
};

// One pin on a node definition. defaultValue is the C++ literal used for an unconnected data input.
// typeGroup > 0 marks a wildcard pin; all pins on a node sharing a group resolve to the same concrete type.
export struct PinDef
{
    std::string  name;
    EDataType    type;
    std::string  defaultValue;
    int          typeGroup = 0;
    std::string  expr;        // for an output pin: its value expression (overrides the node-level emit)
};

// A node type the editor can instantiate. `emit` is a C++ template:
//   $k  -> expression for input pin k (the connected output's expression, else its default literal)
//   #k  -> the statement block produced by following exec output pin k
//   @   -> the selected enum option's code token (see enumTokens)
// Data nodes (isExec == false) have no exec pins and `emit` is a single value expression for output 0.
// A node may also expose `dataEmit`: a value-expression for its data output, used when the node sits
// in the exec flow (isExec) yet still produces a value pulled by downstream data inputs.
export struct NodeDef
{
    std::string          typeId;       // stable id stored in the file (no spaces)
    std::string          displayName;  // shown on the node + palette
    std::string          category;     // palette grouping
    bool                 isExec = false;
    std::vector<PinDef>  inputs;
    std::vector<PinDef>  outputs;
    std::string          emit;
    std::vector<std::string> enumOptions; // dropdown labels for the node's property (empty = no property)
    std::vector<std::string> enumTokens;  // code token per option, parallel to enumOptions, substituted for @
    std::string          dataEmit;        // value expression for this node's data output (when isExec)
};

export uint32 dataTypeColor(EDataType type)
{
    auto rgb = [](uint8 r, uint8 g, uint8 b) -> uint32 { return 0xFF000000u | (uint32(b) << 16) | (uint32(g) << 8) | uint32(r); };
    switch (type)
    {
        case EDataType::Exec:   return rgb(255, 255, 255);
        case EDataType::Bool:   return rgb(255, 80, 80);
        case EDataType::Int:    return rgb(80, 220, 220);
        case EDataType::Float:  return rgb(140, 255, 140);
        case EDataType::Vec3:   return rgb(255, 215, 80);
        case EDataType::String: return rgb(255, 120, 255);
        case EDataType::Wildcard: return rgb(150, 150, 150);
    }
    return rgb(200, 200, 200);
}

// C++ literal used for an unconnected data input of the given type.
export std::string defaultValueForType(EDataType type)
{
    switch (type)
    {
        case EDataType::Bool:   return "false";
        case EDataType::Int:    return "0";
        case EDataType::Float:  return "0.0f";
        case EDataType::Vec3:   return "ScriptVec3{ 0.0f, 0.0f, 0.0f }";
        case EDataType::String: return "\"\"";
        default:                return "0.0f";
    }
}

// The full palette of node types. Returned by const ref so pointers into it stay stable.
export const std::vector<NodeDef>& nodeRegistry()
{
    static const std::vector<NodeDef> registry = []
    {
        using D = EDataType;
        std::vector<NodeDef> r;

        // ---- events / control flow ----
        r.push_back({ "EventUpdate", "Event Update", "Events", true,
            {}, { { "", D::Exec, "" } },
            "#0" });

        r.push_back({ "Branch", "Branch", "Flow", true,
            { { "", D::Exec, "" }, { "condition", D::Bool, "false" } },
            { { "true", D::Exec, "" }, { "false", D::Exec, "" } },
            "if ($1)\n{\n#0}\nelse\n{\n#1}\n" });

        r.push_back({ "ForLoop", "For Loop", "Flow", true,
            { { "", D::Exec, "" }, { "count", D::Int, "10" } },
            { { "body", D::Exec, "" }, { "completed", D::Exec, "" } },
            "for (int i = 0; i < $1; ++i)\n{\n#0}\n#1" });

        // Compares Cond against Compare with the operator picked from the dropdown; the Result output
        // yields A when true, B when false. The exec pins are a pass-through so it can sit in the flow,
        // but Result also works standalone (it is emitted as an inline ternary wherever it is consumed).
        // Cond/Compare share wildcard group 1; A/B/Result share group 2. Each group adopts the concrete
        // type plugged into it, so you can compare ints/floats/bools and select values of any matching type.
        r.push_back({ "Conditional", "Conditional", "Flow", true,
            { { "", D::Exec, "" },
              { "Cond",    D::Wildcard, "0.0f", 1 }, { "Compare", D::Wildcard, "0.0f", 1 },
              { "A",       D::Wildcard, "0.0f", 2 }, { "B",       D::Wildcard, "0.0f", 2 } },
            { { "", D::Exec, "" }, { "Result", D::Wildcard, "", 2 } },
            "#0",
            { "Less than", "Greater than", "Equals", "Not Equals" },
            { "<", ">", "==", "!=" },
            "(($1 @ $2) ? $3 : $4)" });

        // ---- actions ----
        r.push_back({ "Print", "Print", "Debug", true,
            { { "", D::Exec, "" }, { "message", D::String, "\"hello\"" } },
            { { "", D::Exec, "" } },
            "ctx->log($1);\n#0" });

        r.push_back({ "SpawnPointLight", "Spawn Point Light", "Rendering", true,
            { { "", D::Exec, "" },
              { "position",  D::Vec3,  "ScriptVec3{ 0.0f, 2.0f, 0.0f }" },
              { "range",     D::Float, "25.0f" },
              { "color",     D::Vec3,  "ScriptVec3{ 1.0f, 0.6f, 0.2f }" },
              { "intensity", D::Float, "60.0f" } },
            { { "", D::Exec, "" } },
            "ctx->spawnPointLight($1, $2, $3, $4);\n#0" });

        r.push_back({ "SetSun", "Set Sun", "Rendering", true,
            { { "", D::Exec, "" },
              { "direction", D::Vec3,  "ScriptVec3{ -0.3f, -1.0f, -0.2f }" },
              { "color",     D::Vec3,  "ScriptVec3{ 1.0f, 1.0f, 1.0f }" },
              { "intensity", D::Float, "3.0f" } },
            { { "", D::Exec, "" } },
            "ctx->setSun($1, $2, $3);\n#0" });

        // ---- inputs / time ----
        r.push_back({ "GetElapsedTime", "Get Elapsed Time", "Time", false,
            {}, { { "seconds", D::Float, "" } },
            "ctx->elapsedSeconds()" });

        r.push_back({ "GetDeltaTime", "Get Delta Time", "Time", false,
            {}, { { "seconds", D::Float, "" } },
            "ctx->deltaSeconds()" });

        r.push_back({ "IsKeyDown", "Is Key Down", "Input", false,
            { { "key", D::String, "\"Space\"" } }, { { "down", D::Bool, "" } },
            "(ctx->isKeyDown($0) != 0)" });

        // ---- math ----
        r.push_back({ "AddFloat", "Add", "Math", false,
            { { "a", D::Float, "0.0f" }, { "b", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "($0 + $1)" });

        r.push_back({ "SubFloat", "Subtract", "Math", false,
            { { "a", D::Float, "0.0f" }, { "b", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "($0 - $1)" });

        r.push_back({ "MulFloat", "Multiply", "Math", false,
            { { "a", D::Float, "1.0f" }, { "b", D::Float, "1.0f" } }, { { "result", D::Float, "" } },
            "($0 * $1)" });

        r.push_back({ "Sin", "Sin", "Math", false,
            { { "x", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "sinf($0)" });

        r.push_back({ "Cos", "Cos", "Math", false,
            { { "x", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "cosf($0)" });

        r.push_back({ "MakeVec3", "Make Vec3", "Math", false,
            { { "x", D::Float, "0.0f" }, { "y", D::Float, "0.0f" }, { "z", D::Float, "0.0f" } },
            { { "vec", D::Vec3, "" } },
            "ScriptVec3{ $0, $1, $2 }" });

        r.push_back({ "ConstFloat", "Float", "Constants", false,
            { { "value", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "$0" });

        // ---- entity (the script's owning entity, exposed as `self`) ----
        // Get Entity: every readable entity property as a separate output (each output's `expr` field).
        r.push_back({ "GetEntity", "Get Entity", "Entity", false,
            {},
            { { "Position",  D::Vec3,   "", 0, "ctx->entityGetPosition(ctx->self)" },
              { "Scale",     D::Float,  "", 0, "ctx->entityGetScale(ctx->self)" },
              { "Rotation",  D::Vec3,   "", 0, "ctx->entityGetRotation(ctx->self)" },
              { "Forward",   D::Vec3,   "", 0, "ctx->entityGetForward(ctx->self)" },
              { "Right",     D::Vec3,   "", 0, "ctx->entityGetRight(ctx->self)" },
              { "Up",        D::Vec3,   "", 0, "ctx->entityGetUp(ctx->self)" },
              { "Name",      D::String, "", 0, "ctx->entityGetName(ctx->self)" },
              { "Enabled",   D::Bool,   "", 0, "(ctx->entityGetEnabled(ctx->self) != 0)" },
              { "Children",  D::Int,    "", 0, "ctx->entityGetChildCount(ctx->self)" },
              { "Bounds R",  D::Float,  "", 0, "ctx->entityGetBoundsRadius(ctx->self)" } },
            "" });

        // Set Entity: writes only the inputs you actually connect (the ?k{...} conditional blocks).
        r.push_back({ "SetEntity", "Set Entity", "Entity", true,
            { { "", D::Exec, "" },
              { "Position", D::Vec3,  "ScriptVec3{ 0.0f, 0.0f, 0.0f }" },
              { "Scale",    D::Float, "1.0f" },
              { "Rotation", D::Vec3,  "ScriptVec3{ 0.0f, 0.0f, 0.0f }" },
              { "Enabled",  D::Bool,  "true" } },
            { { "", D::Exec, "" } },
            "?1{ctx->entitySetPosition(ctx->self, $1);\n}?2{ctx->entitySetScale(ctx->self, $2);\n}"
            "?3{ctx->entitySetRotation(ctx->self, $3);\n}?4{ctx->entitySetEnabled(ctx->self, $4);\n}#0" });

        r.push_back({ "SetAnimFloat", "Set Anim Float", "Entity", true,
            { { "", D::Exec, "" }, { "param", D::String, "\"speed\"" }, { "value", D::Float, "0.0f" } },
            { { "", D::Exec, "" } },
            "ctx->entitySetAnimFloat(ctx->self, $1, $2);\n#0" });

        r.push_back({ "SetAnimTrigger", "Set Anim Trigger", "Entity", true,
            { { "", D::Exec, "" }, { "param", D::String, "\"attack\"" } },
            { { "", D::Exec, "" } },
            "ctx->entitySetAnimTrigger(ctx->self, $1);\n#0" });

        return r;
    }();
    return registry;
}

export const NodeDef* findNodeDef(const std::string& typeId)
{
    for (const NodeDef& def : nodeRegistry())
        if (def.typeId == typeId)
            return &def;
    return nullptr;
}

} // namespace NodeEditor
