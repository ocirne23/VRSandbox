export module UI.NodeEditor.NodeDef;

import Core;

namespace NodeEditor
{

export const char INDENT_UP   = '\x01';
export const char INDENT_DOWN = '\x02';
export const char HOIST = '\x03';

// Data carried on a pin / connection. Exec is control flow; the rest are value types.
// Wildcard is an unresolved generic pin that adopts the concrete type it gets connected to.
export enum EDataType : uint8
{
    Exec,
    Bool,
    Int,
    Float,
    Vec3,
    String,
    Wildcard,
};

// Read/write capability of a pin's value. On an INPUT it is what the node requires of its source; on an
// OUTPUT it is what the produced value supports (a plain rvalue is Readable; a variable is ReadWritable).
// A link is allowed only if the output provides every capability the input requires (see mutableBits).
export enum class EMutableType : uint8
{
    Readable,
    Writable,
    ReadWritable
};

// read = 1, write = 2. A connection output->input is allowed when the output provides every capability the
// input requires: (inputBits & ~outputBits) == 0.
export uint8 mutableBits(EMutableType m)
{
    switch (m)
    {
        case EMutableType::Readable:     return 1;
        case EMutableType::Writable:     return 2;
        case EMutableType::ReadWritable: return 3;
    }
    return 1;
}

// One pin on a node definition. defaultValue is the C++ literal used for an unconnected data input.
// typeGroup > 0 marks a wildcard pin; all pins on a node sharing a group resolve to the same concrete type.
export struct PinDef
{
    std::string  name;
    EDataType    type;
    std::string  defaultValue;
    int          typeGroup = 0;
    std::string  expr;        // for an output pin: its value expression (overrides the node-level emit)
    EMutableType mutability = EMutableType::Readable; // read/write capability; restricts what may connect here
};

// A node type the editor can instantiate. `emit` is a C++ template:
//   $k  -> expression for input pin k (the connected output's expression, else its default literal)
//   #k  -> the statement block produced by following exec output pin k
//   @   -> the selected enum option's code token (see enumTokens), unique node idx if no enumTokens provided
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
        case EDataType::Vec3:   return "glm::vec3{ 0.0f, 0.0f, 0.0f }";
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

        r.push_back({ "If", "If", "Flow", true,
            { { "", D::Exec, "" }, { "condition", D::Bool, "true" } },
            { { "true", D::Exec, "" }, { "break", D::Exec, "" } },
            "if ($1)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\n#1" });

        r.push_back({ "IfElse", "If Else", "Flow", true,
            { { "", D::Exec, "" }, { "condition", D::Bool, "true" } },
            { { "true", D::Exec, "" }, { "false", D::Exec, "" }, { "break", D::Exec, "" } },
            "if ($1)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\nelse\n{\n" + std::string(1, INDENT_UP) + "#1" + std::string(1, INDENT_DOWN) + "}\n#2" });

        r.push_back({ "ForLoop", "For Loop", "Flow", true,
            { { "", D::Exec, "" }, { "count", D::Int, "10" } },
            { { "body", D::Exec, "" }, { "completed", D::Exec, "" }, { "idx", D::Int, "", 0, "i@"}},
            "for (int i@ = 0; i@ < $1; ++i@)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "\n}\n#1" });

        r.push_back({ "Conditional", "Conditional", "Flow", true,
            { { "", D::Exec, "" }, { "Cond", D::Wildcard, "0.0f", 1 }, { "Compare", D::Wildcard, "0.0f", 1 }, { "A", D::Wildcard, "0.0f", 2 }, { "B", D::Wildcard, "0.0f", 2 } },
            { { "", D::Exec, "" }, { "Result", D::Wildcard, "", 2 } },
            "#0",
            { "Less than", "Greater than", "Equals", "Not Equals" },
            { "<", ">", "==", "!=" },
            "(($1 @ $2) ? $3 : $4)" });

        r.push_back({ "Cast", "Cast", "Flow", false,
            { { "Cast",   D::Wildcard, "0.0f", 1 } },
            { { "Result", D::Wildcard, "", 2 } },
            "((@)$0)",
            { "int", "float", "bool" },
            { "int", "float", "bool" }
        });

        // ---- actions ----
        r.push_back({ "Print", "Print", "Debug", true,
            { { "", D::Exec, "" }, { "message", D::String, "\"hello\"" } },
            { { "", D::Exec, "" } },
            "ctx->log($1);\n#0" });

        r.push_back({ "Printf", "Printf", "Debug", true,
            { { "", D::Exec, "" },
                { "message", D::String, "\"%f\"" },
                { "message", D::Wildcard, "0.0f", 1 } },
            { { "", D::Exec, "" } },
            "ctx->logf($1, $2);\n#0" });

        r.push_back({ "SpawnPointLight", "Spawn Point Light", "Rendering", true,
            { { "", D::Exec, "" },
              { "position",  D::Vec3,  "glm::vec3{ 0.0f, 2.0f, 0.0f }" },
              { "range",     D::Float, "25.0f" },
              { "color",     D::Vec3,  "glm::vec3{ 1.0f, 0.6f, 0.2f }" },
              { "intensity", D::Float, "60.0f" } },
            { { "", D::Exec, "" } },
            "ctx->spawnPointLight($1, $2, $3, $4);\n#0" });

        r.push_back({ "SetSun", "Set Sun", "Rendering", true,
            { { "", D::Exec, "" },
              { "direction", D::Vec3,  "glm::vec3{ -0.3f, -1.0f, -0.2f }" },
              { "color",     D::Vec3,  "glm::vec3{ 1.0f, 1.0f, 1.0f }" },
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

        r.push_back({ "Float", "Var Float", "Variables", false,
            { { "value", D::Float, "0.0f" }},
            { { "f@", D::Float, "", 0, std::string("float f@ = $0;\n") + HOIST + "f@", EMutableType::ReadWritable } },
            "" });

        r.push_back({ "Int", "Var Int", "Variables", false,
            { { "value", D::Int, "0" }},
            { { "i@", D::Int, "", 0, std::string("int i@ = $0;\n") + HOIST + "i@", EMutableType::ReadWritable } },
            "" });

        r.push_back({ "Bool", "Var Bool", "Variables", false,
            { { "value", D::Bool, "false" }},
            { { "b@", D::Bool, "", 0, std::string("bool b@ = $0;\n") + HOIST + "b@", EMutableType::ReadWritable } },
            "" });

        r.push_back({ "ConstFloat", "Const Float", "Variables", false,
            { { "value", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "$0" });

        r.push_back({ "ConstInt", "Const Int", "Variables", false,
            { { "value", D::Int, "0" } }, { { "result", D::Int, "" } },
            "$0" });

		r.push_back({ "ConstBool", "Const Bool", "Variables", false,
			{ { "value", D::Bool, "false" } }, { { "result", D::Bool, "" } },
			"$0" });

        r.push_back({ "IsKeyDown", "Is Key Down", "Input", false,
            { { "key", D::String, "\"Space\"" } }, { { "down", D::Bool, "" } },
            "(ctx->isKeyDown($0) != 0)" });

        // ---- math ----
        r.push_back({ "Add", "Add", "Math", false,
            { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
            "($0 + $1)" });

        r.push_back({ "Increment", "Increment", "Math", true,
            { { "", D::Exec, "" }, { "a", D::Wildcard, "0.0f", 1, "", EMutableType::ReadWritable}, {"b", D::Wildcard, "0.0f", 1}},
            { { "", D::Exec, "" } },
            "($1 += $2)" });

        r.push_back({ "Sub", "Subtract", "Math", false,
            { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
            "($0 - $1)" });

        r.push_back({ "Mul", "Multiply", "Math", false,
            { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
            "($0 * $1)" });

        r.push_back({ "Div", "Divide", "Math", false,
            { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
            "($0 / $1)" });

        r.push_back({ "Sin", "Sin", "Math", false,
            { { "x", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "sinf($0)" });

        r.push_back({ "Cos", "Cos", "Math", false,
            { { "x", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
            "cosf($0)" });

        r.push_back({ "MakeVec3", "Make Vec3", "Math", false,
            { { "x", D::Float, "0.0f" }, { "y", D::Float, "0.0f" }, { "z", D::Float, "0.0f" } },
            { { "vec", D::Vec3, "" } },
            "glm::vec3{ $0, $1, $2 }" });

        r.push_back({ "SplitVec3", "Split Vec3", "Math", false,
            { { "vec", D::Vec3, "" } },
            { { "x", D::Float, "", 0, "$0.x"}, {"y", D::Float, "", 0, "$0.y"}, {"z", D::Float, "", 0, "$0.z"}}
            });

        // ---- vector math (Vec3 is glm::vec3 in compiled scripts, so these use glm operators/functions) ----
        r.push_back({ "AddVec3", "Add Vec3", "Math", false,
            { { "a", D::Vec3, "" }, { "b", D::Vec3, "" } }, { { "result", D::Vec3, "" } },
            "($0 + $1)" });

        r.push_back({ "SubVec3", "Subtract Vec3", "Math", false,
            { { "a", D::Vec3, "" }, { "b", D::Vec3, "" } }, { { "result", D::Vec3, "" } },
            "($0 - $1)" });

        r.push_back({ "ScaleVec3", "Scale Vec3", "Math", false,
            { { "vec", D::Vec3, "" }, { "scale", D::Float, "1.0f" } }, { { "result", D::Vec3, "" } },
            "($0 * $1)" });

        r.push_back({ "DotVec3", "Dot", "Math", false,
            { { "a", D::Vec3, "" }, { "b", D::Vec3, "" } }, { { "result", D::Float, "" } },
            "glm::dot($0, $1)" });

        r.push_back({ "CrossVec3", "Cross", "Math", false,
            { { "a", D::Vec3, "" }, { "b", D::Vec3, "" } }, { { "result", D::Vec3, "" } },
            "glm::cross($0, $1)" });

        r.push_back({ "LengthVec3", "Length", "Math", false,
            { { "vec", D::Vec3, "" } }, { { "result", D::Float, "" } },
            "glm::length($0)" });

        r.push_back({ "NormalizeVec3", "Normalize", "Math", false,
            { { "vec", D::Vec3, "" } }, { { "result", D::Vec3, "" } },
            "glm::normalize($0)" });


        // ---- entity (the script's owning entity, exposed as `self`) ----
        // Get Entity: every readable entity property as a separate output (each output's `expr` field).
        r.push_back({ "GetEntity", "Get Entity", "Entity", false,
            {},
            { { "Position",  D::Vec3,   "", 0, "ctx->entityGetPosition(self)" },
              { "Scale",     D::Float,  "", 0, "ctx->entityGetScale(self)" },
              { "Rotation",  D::Vec3,   "", 0, "ctx->entityGetRotation(self)" },
              { "Forward",   D::Vec3,   "", 0, "ctx->entityGetForward(self)" },
              { "Right",     D::Vec3,   "", 0, "ctx->entityGetRight(self)" },
              { "Up",        D::Vec3,   "", 0, "ctx->entityGetUp(self)" },
              { "Name",      D::String, "", 0, "ctx->entityGetName(self)" },
              { "Enabled",   D::Bool,   "", 0, "(ctx->entityGetEnabled(self) != 0)" },
              { "Children",  D::Int,    "", 0, "ctx->entityGetChildCount(self)" },
              { "Bounds R",  D::Float,  "", 0, "ctx->entityGetBoundsRadius(self)" } },
            "" });

        // Set Entity: writes only the inputs you actually connect (the ?k{...} conditional blocks).
        r.push_back({ "SetEntity", "Set Entity", "Entity", true,
            { { "", D::Exec, "" },
              { "Position", D::Vec3,  "glm::vec3{ 0.0f, 0.0f, 0.0f }" },
              { "Scale",    D::Float, "1.0f" },
              { "Rotation", D::Vec3,  "glm::vec3{ 0.0f, 0.0f, 0.0f }" },
              { "Enabled",  D::Bool,  "true" } },
            { { "", D::Exec, "" } },
            "?1{ctx->entitySetPosition(self, $1);\n}?2{ctx->entitySetScale(self, $2);\n}"
            "?3{ctx->entitySetRotation(self, $3);\n}?4{ctx->entitySetEnabled(self, $4);\n}#0" });

        // Spawn Entity: queues an asset/prefab to spawn at a world position (drained by App after update).
        r.push_back({ "SpawnEntity", "Spawn Entity", "Entity", true,
            { { "", D::Exec, "" },
              { "asset",    D::String, "\"Entities/character.pre\"" },
              { "position", D::Vec3,   "glm::vec3{ 0.0f, 0.0f, 0.0f }" } },
            { { "", D::Exec, "" } },
            "ctx->spawnEntity($1, $2);\n#0" });

        // Destroy Entity: queues this script's owning entity (self) for removal.
        r.push_back({ "DestroyEntity", "Destroy Entity", "Entity", true,
            { { "", D::Exec, "" } },
            { { "", D::Exec, "" } },
            "ctx->destroyEntity(self);\n#0" });

        r.push_back({ "SetAnimFloat", "Set Anim Float", "Entity", true,
            { { "", D::Exec, "" }, { "param", D::String, "\"speed\"" }, { "value", D::Float, "0.0f" } },
            { { "", D::Exec, "" } },
            "ctx->entitySetAnimFloat(self, $1, $2);\n#0" });

        r.push_back({ "SetAnimTrigger", "Set Anim Trigger", "Entity", true,
            { { "", D::Exec, "" }, { "param", D::String, "\"attack\"" } },
            { { "", D::Exec, "" } },
            "ctx->entitySetAnimTrigger(self, $1);\n#0" });

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
