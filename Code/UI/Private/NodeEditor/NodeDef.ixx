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
//   ?k  {...} -> conditional block: emit the contents if input pin k is connected, else skip it
//   @   -> the selected enum option's code token (see enumTokens), unique node idx if no enumTokens provided
// Data nodes (isExec == false) have no exec pins and `emit` is a single value expression for output 0.
// An exec node that also produces a value (e.g. Conditional) puts that value expression on the data output
// pin's `expr` (PinDef::expr), since `emit` on an exec node is a statement, not an expression.
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

// The Script Data node is special: user-defined members instead of static pins (see nodeRegistry / codegen).
export bool isScriptDataType(std::string_view typeId) { return typeId == "ScriptData"; }

// The Label node is a resizable comment box (a group). It has no pins and emits no code — it only organizes
// and documents the graph, and drags any nodes inside it along when moved.
export bool isLabelType(std::string_view typeId) { return typeId == "Label"; }

// The Reroute node is a draggable waypoint on a link: one input + one output pin of the carried type, drawn
// as a small dot. It emits no code — codegen passes straight through it (see realSourceOfInput). Created by
// double-clicking a link, not from the palette.
export bool isRerouteType(std::string_view typeId) { return typeId == "Reroute"; }

// The On Event node is special like Script Data: user-defined named entries instead of static pins, each one
// a plain Exec output. Multiple On Event nodes stay in sync (same entry set); codegen dispatches a fired
// event name to the matching entry's exec chain (see Scene::generateCpp / applyEventEdit).
export bool isEventEntryType(std::string_view typeId) { return typeId == "OnEvent"; }

// Serialization token for any pin data type (covers Exec/String too, unlike memberTypeToken).
export const char* dataTypeToken(EDataType type)
{
    switch (type)
    {
        case EDataType::Exec:     return "Exec";
        case EDataType::Bool:     return "Bool";
        case EDataType::Int:      return "Int";
        case EDataType::Float:    return "Float";
        case EDataType::Vec3:     return "Vec3";
        case EDataType::String:   return "String";
        case EDataType::Wildcard: return "Wild";
    }
    return "Float";
}

export EDataType dataTypeFromToken(std::string_view token)
{
    for (int i = 0; i <= (int)EDataType::Wildcard; ++i)
        if (token == dataTypeToken((EDataType)i))
            return (EDataType)i;
    return EDataType::Float;
}

// Value types a Script Data member (persistent struct field) may have. String is excluded on purpose: an
// std::string can't live in the POD block that crosses the script ABI.
export inline constexpr EDataType memberTypes[] = { EDataType::Int, EDataType::Float, EDataType::Bool, EDataType::Vec3 };

// C++ type name for a member field, used both for the struct declaration and the in-node type button label.
export const char* memberCppType(EDataType type)
{
    switch (type)
    {
        case EDataType::Int:  return "int";
        case EDataType::Float: return "float";
        case EDataType::Bool: return "bool";
        case EDataType::Vec3: return "glm::vec3";
        default:              return "float";
    }
}

// Short token stored in the //@member serialization line (round-trips a member's type).
export const char* memberTypeToken(EDataType type)
{
    switch (type)
    {
        case EDataType::Int:  return "int";
        case EDataType::Float: return "float";
        case EDataType::Bool: return "bool";
        case EDataType::Vec3: return "Vec3";
        default:              return "float";
    }
}

export EDataType memberTypeFromToken(std::string_view token)
{
    for (EDataType t : memberTypes)
        if (token == memberTypeToken(t))
            return t;
    return EDataType::Float;
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
    static const std::vector<NodeDef> registry = []{
    using D = EDataType;
    std::vector<NodeDef> r;

    // ---- organization ----
    // Comment/label box (isLabelType): a movable, resizable group with an editable caption. Pure annotation,
    // no pins, no generated code — nodes dragged inside it move with it.
    r.push_back({ "Label", "Label", "Comment", false, {}, {}, "" });

    // Reroute waypoint (isRerouteType): pins are added dynamically to match the link it's placed on; created
    // by double-clicking a link, so it's hidden from the palette.
    r.push_back({ "Reroute", "Reroute", "", false, {}, {}, "" });

    // ---- events / control flow ----
    r.push_back({ "Update", "Update", "Events", true,
        {}, { { "", D::Exec, "" } },
        "#0" });

    r.push_back({ "OnSpawn", "On Spawn", "Events", true,
        {}, { { "", D::Exec, "" } },
        "#0" });

    r.push_back({ "OnDestroy", "On Destroy", "Events", true,
        {}, { { "", D::Exec, "" } },
        "#0" });

    // On Event is special-cased (isEventEntryType): its exec output pins are user-defined named entries,
    // added/removed/renamed through the editor and fired at runtime by name (ScriptComponent::fireEvent).
    r.push_back({ "OnEvent", "On Event", "Events", true, {}, {}, "" });

    r.push_back({ "If", "If", "Flow", true,
        { { "", D::Exec, "" }, { "Cond", D::Wildcard, "0.0f", 1 }, { "Comp", D::Wildcard, "0.0f", 1 } },
        { { "true", D::Exec, "" }, { "break", D::Exec, "" } },
        "if ($1 @ $2)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\n#1",
        { "Less than", "Greater than", "Equals", "Not Equals" },
        { "<", ">", "==", "!=" } });

    r.push_back({ "IfElse", "If Else", "Flow", true,
        { { "", D::Exec, "" }, { "Cond", D::Wildcard, "0.0f", 1 }, { "Comp", D::Wildcard, "0.0f", 1 } },
        { { "true", D::Exec, "" }, { "false", D::Exec, "" }, { "break", D::Exec, "" } },
        "if ($1 @ $2)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\nelse\n{\n" + std::string(1, INDENT_UP) + "#1" + std::string(1, INDENT_DOWN) + "}\n#2",
        { "Less than", "Greater than", "Equals", "Not Equals" },
        { "<", ">", "==", "!=" } });

    r.push_back({ "ForLoop", "For Loop", "Flow", true,
        { { "", D::Exec, "" }, { "start", D::Int, "0" }, { "count", D::Int, "10" } },
        { { "body", D::Exec, "" }, { "completed", D::Exec, "" }, { "idx", D::Int, "", 0, "i@"}},
        "for (int i@ = $1; i@ < $2; ++i@)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\n#1" });

    r.push_back({ "Break", "Break", "Flow", true,
        {{ "", D::Exec, "" }}, 
        {},
        "break;\n"});

    r.push_back({ "Conditional", "Conditional", "Flow", true,
        { { "", D::Exec, "" }, { "Cond", D::Wildcard, "0.0f", 1 }, { "Comp", D::Wildcard, "0.0f", 1 }, { "A", D::Wildcard, "0.0f", 2 }, { "B", D::Wildcard, "0.0f", 2 } },
        { { "", D::Exec, "" }, { "Result", D::Wildcard, "", 2, "(($1 @ $2) ? $3 : $4)" } },
        "#0",
        { "Less than", "Greater than", "Equals", "Not Equals" },
        { "<", ">", "==", "!=" } });

    r.push_back({ "Cast", "Cast", "Flow", false,
        { { "Cast",   D::Wildcard, "0.0f", 1 } },
        { { "Result", D::Wildcard, "", 2 } },
        "((@)$0)",
        { "int", "float", "bool" },
        { "int", "float", "bool" }});

    // ---- variables ----
    // Script Data is special-cased everywhere (isScriptDataType): its output pins are user-defined members
    // edited in the node, and codegen turns them into a persistent `struct ScriptData`. It carries no static
    // pins here — the members are added through the editor and serialized as //@member lines.
    r.push_back({ "ScriptData", "Script Data", "Variables", false, {}, {}, "" });

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

    r.push_back({ "IsKeyDown", "Is Key Down", "Input", false,
        { { "key", D::String, "\"Space\"" } }, { { "down", D::Bool, "" } },
        "(ctx->isKeyDown($0) != 0)" });

    // ---- math ----
    // -- mutable math nodes -- 
    r.push_back({ "Set", "Set", "Math", true,
        { { "", D::Exec, "" }, { "a", D::Wildcard, "0.0f", 1, "", EMutableType::ReadWritable}, {"b", D::Wildcard, "0.0f", 1}},
        { { "", D::Exec, "" } },
        "$1 = $2;\n#0" });

    r.push_back({ "SetAdd", "Set Add", "Math", true,
        { { "", D::Exec, "" }, { "a", D::Wildcard, "0.0f", 1, "", EMutableType::ReadWritable}, {"b", D::Wildcard, "0.0f", 1}},
        { { "", D::Exec, "" } },
        "$1 += $2;\n#0" });

    r.push_back({ "SetSub", "Set Sub", "Math", true,
        { { "", D::Exec, "" }, { "a", D::Wildcard, "0.0f", 1, "", EMutableType::ReadWritable}, {"b", D::Wildcard, "0.0f", 1}},
        { { "", D::Exec, "" } },
        "$1 -= $2;\n#0" });

    r.push_back({ "SetMul", "Set Mul", "Math", true,
        { { "", D::Exec, "" }, { "a", D::Wildcard, "0.0f", 1, "", EMutableType::ReadWritable}, {"b", D::Wildcard, "0.0f", 1}},
        { { "", D::Exec, "" } },
        "$1 *= $2;\n#0" });

    r.push_back({ "SetDiv", "Set Div", "Math", true,
        { { "", D::Exec, "" }, { "a", D::Wildcard, "0.0f", 1, "", EMutableType::ReadWritable}, {"b", D::Wildcard, "0.0f", 1}},
        { { "", D::Exec, "" } },
        "$1 /= $2;\n#0" });

	// -- immutable math nodes --
    r.push_back({ "Add", "Add", "Math", false,
        { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
        "($0 + $1)" });

    r.push_back({ "Sub", "Subtract", "Math", false,
        { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
        "($0 - $1)" });

    r.push_back({ "Mul", "Multiply", "Math", false,
        { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
        "($0 * $1)" });

    r.push_back({ "Div", "Divide", "Math", false,
        { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
        "($0 / $1)" });

    r.push_back({ "Modulo", "Modulo", "Math", false,
        { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "result", D::Wildcard, "", 1 } },
        "($0 % $1)" });

    r.push_back({ "Sin", "Sin", "Math", false,
        { { "x", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
        "sinf($0)" });

    r.push_back({ "Cos", "Cos", "Math", false,
        { { "x", D::Float, "0.0f" } }, { { "result", D::Float, "" } },
        "cosf($0)" });

    // ---- vector math ----
    r.push_back({ "MakeVec3", "Make Vec3", "Math", false,
        { { "x", D::Float, "0.0f" }, { "y", D::Float, "0.0f" }, { "z", D::Float, "0.0f" } },
        { { "vec", D::Vec3, "" } },
        "glm::vec3{ $0, $1, $2 }" });


    r.push_back({ "SplitVec3", "Split Vec3", "Math", false,
        { { "vec", D::Vec3, "" } },
        { { "x", D::Float, "", 0, "v@.x" }, { "y", D::Float, "", 0, "v@.y" }, { "z", D::Float, "", 0, "v@.z" } },
        std::string("glm::vec3 v@ = $0;\n") + HOIST + "v@"});

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
