export module UI.NodeEditor.NodeDef;

import Core;

namespace NodeEditor
{

export const char INDENT_UP   = '\x01';
export const char INDENT_DOWN = '\x02';
export const char HOIST = '\x03';
export const char ENUM_TOKEN = '\x04';

// Data carried on a pin / connection. Exec is control flow; the rest are value types.
// Wildcard is an unresolved generic pin that adopts the concrete type it gets connected to.
export enum EDataType : uint8
{
    Exec,
    Bool,
    Int,
    Float,
    Vec3,
    Quat,
    String,
    Entity,   // opaque Entity* handle (script-side pointer, never dereferenced by graph code directly)
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

// One pin on a node definition. defaultValue is the C++ literal used for an unconnected data input — EXCEPT
// for a String pin, whose default is stored as raw text (no surrounding quotes; the editor edits it directly)
// and gets wrapped into a C++ string literal at codegen time (see emitDataExpr).
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
//   @   -> replaced with unique node idx
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
    std::vector<std::string> enumTokens;  // code token per option, parallel to enumOptions, substituted for ENUM_TOKEN
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
        case EDataType::Quat:   return rgb(180, 130, 255);
        case EDataType::String: return rgb(255, 120, 255);
        case EDataType::Entity: return rgb(255, 150, 60);
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

// Function boundary + call nodes. A .scr can define multiple named functions; each is a (Function Input,
// Function Output) pair sharing a function name. Function Input holds a fixed Exec output plus user-defined
// typed OUTPUT pins (the parameters); Function Output holds a fixed Exec input plus user-defined typed INPUT
// pins (the return values). A Function Call node (created by importing a function, hidden from the palette)
// mirrors that signature and, at codegen, expands to a real C++ function call. See Scene::generateCpp.
export bool isFunctionInputType(std::string_view typeId)  { return typeId == "FunctionInput"; }
export bool isFunctionOutputType(std::string_view typeId) { return typeId == "FunctionOutput"; }
export bool isFunctionCallType(std::string_view typeId)   { return typeId == "FunctionCall"; }

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
        case EDataType::Quat:     return "Quat";
        case EDataType::String:   return "String";
        case EDataType::Entity:   return "Entity";
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

// C++ literal used for an unconnected data input of the given type. A String's default is RAW text (no
// quotes) — codegen wraps it into a literal (see emitDataExpr) — so its default here is the empty string.
export std::string defaultValueForType(EDataType type)
{
    switch (type)
    {
        case EDataType::Bool:   return "false";
        case EDataType::Int:    return "0";
        case EDataType::Float:  return "0.0f";
        case EDataType::Vec3:   return "glm::vec3{ 0.0f, 0.0f, 0.0f }";
        case EDataType::Quat:   return "glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }"; // identity (w,x,y,z)
        case EDataType::String: return "";
        case EDataType::Entity: return "self";
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
    r.push_back({ "Label", "Label", "Debug", false, {}, {}, "" });

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
        "if ($1 " + std::string(1, ENUM_TOKEN) + " $2)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\n#1",
        { "Less than", "Greater than", "Equals", "Not Equals" },
        { "<", ">", "==", "!=" } });

    r.push_back({ "IfElse", "If Else", "Flow", true,
        { { "", D::Exec, "" }, { "Cond", D::Wildcard, "0.0f", 1 }, { "Comp", D::Wildcard, "0.0f", 1 } },
        { { "true", D::Exec, "" }, { "false", D::Exec, "" }, { "break", D::Exec, "" } },
        "if ($1 " + std::string(1, ENUM_TOKEN) + " $2)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\nelse\n{\n" + std::string(1, INDENT_UP) + "#1" + std::string(1, INDENT_DOWN) + "}\n#2",
        { "Less than", "Greater than", "Equals", "Not Equals" },
        { "<", ">", "==", "!=" } });
    
    r.push_back({ "ForLoop", "For Loop", "Flow", true,
        { { "", D::Exec, "" }, { "start", D::Int, "0" }, { "count", D::Int, "10" } },
        { { "body", D::Exec, "" }, { "break", D::Exec, "" }, { "idx", D::Int, "", 0, "i@"}},
        "for (int i@ = $1; i@ " + std::string(1, ENUM_TOKEN) + " $2; ++i@)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\n#1",
        { "Less than", "Less or Equals", "Equals", "Greater or Equals", "Greater than", "Not Equals" },
        { "<", "<=", "==", ">=", ">", "!=" } });

    r.push_back({ "Break", "Break", "Flow", true,
        {{ "", D::Exec, "" }}, 
        {},
        "break;\n"});

    r.push_back({ "Conditional", "Conditional", "Flow", true,
        { { "", D::Exec, "" }, { "Cond", D::Wildcard, "0.0f", 1 }, { "Comp", D::Wildcard, "0.0f", 1 }, { "A", D::Wildcard, "0.0f", 2 }, { "B", D::Wildcard, "0.0f", 2 } },
        { { "", D::Exec, "" }, { "res", D::Wildcard, "", 2, "(($1 @ $2) ? $3 : $4)" } },
        "#0",
        { "Less than", "Greater than", "Equals", "Not Equals" },
        { "<", ">", "==", "!=" } });

    r.push_back({ "Cast", "Cast", "Flow", false,
        { { "Cast",   D::Wildcard, "0.0f", 1 } },
        { { "res", D::Wildcard, "", 2 } },
        "((" + std::string(1, ENUM_TOKEN) + ")$0)",
        { "int", "float", "bool" },
        { "int", "float", "bool" }});

    // ---- functions ----
    // Function boundary nodes (special-cased in codegen). Function Input is the entry: a fixed Exec output
    // (pin 0) plus user-added typed parameter outputs. Function Output is the exit: a fixed Exec input (pin 0)
    // plus user-added typed return inputs. Both carry a function name so one .scr can hold several functions.
    r.push_back({ "FunctionInput",  "Function Input",  "Functions", true, {}, { { "", D::Exec, "" } }, "" });
    r.push_back({ "FunctionOutput", "Function Output", "Functions", true, { { "", D::Exec, "" } }, {}, "" });

    // Function Call: created by importing a function (hidden from the palette, like Reroute). Its pins are
    // rebuilt from the referenced function's signature; codegen turns it into a C++ call.
    r.push_back({ "FunctionCall", "Function Call", "Functions", true, {}, {}, "" });

    // ---- variables ----
    // Script Data is special-cased everywhere (isScriptDataType): its output pins are user-defined members
    // edited in the node, and codegen turns them into a persistent `struct ScriptData`. It carries no static
    // pins here — the members are added through the editor and serialized as //@member lines.
    r.push_back({ "ScriptData", "Script Data", "Variables", false, {}, {}, "" });

    r.push_back({ "Float", "Var Float", "Variables", false,
        { { "val", D::Float, "0.0f" }},
        { { "f@", D::Float, "", 0, std::string("float f@ = $0;\n") + HOIST + "f@", EMutableType::ReadWritable } },
        "" });

    r.push_back({ "Int", "Var Int", "Variables", false,
        { { "val", D::Int, "0" }},
        { { "i@", D::Int, "", 0, std::string("int i@ = $0;\n") + HOIST + "i@", EMutableType::ReadWritable } },
        "" });

    r.push_back({ "Bool", "Var Bool", "Variables", false,
        { { "val", D::Bool, "false" }},
        { { "b@", D::Bool, "", 0, std::string("bool b@ = $0;\n") + HOIST + "b@", EMutableType::ReadWritable } },
        "" });

    r.push_back({ "ConstFloat", "Const Float", "Variables", false,
        { { "val", D::Float, "0.0f" } }, { { "res", D::Float, "" } },
        "$0" });

    r.push_back({ "ConstInt", "Const Int", "Variables", false,
        { { "val", D::Int, "0" } }, { { "res", D::Int, "" } },
        "$0" });

    r.push_back({ "ConstBool", "Const Bool", "Variables", false,
        { { "val", D::Bool, "false" } }, { { "res", D::Bool, "" } },
        "$0" });

    // ---- actions ----
    r.push_back({ "Print", "Print", "Debug", true,
        { { "", D::Exec, "" }, { "message", D::String, "hello" } },
        { { "", D::Exec, "" } },
        "ctx->log($1);\n#0" });

    r.push_back({ "Printf", "Printf", "Debug", true,
        { { "", D::Exec, "" },
            { "message", D::String, "%f" },
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
    // deltaSeconds/elapsedSeconds are plain fields on ScriptContext (refreshed once a frame), not calls.
    r.push_back({ "GetElapsedTime", "Get Elapsed Time", "Time", false,
        {}, { { "sec", D::Float, "" } },
        "ctx->elapsedSeconds" });

    r.push_back({ "GetDeltaTime", "Get Delta Time", "Time", false,
        {}, { { "sec", D::Float, "" } },
        "ctx->deltaSeconds" });

    r.push_back({ "IsKeyDown", "Is Key Down", "Events", false,
        { { "key", D::String, "Space" } }, { { "down", D::Bool, "" } },
        "(ctx->isKeyDown($0) != 0)" });

    // ---- camera (the active render camera, snapshotted once a frame onto ScriptContext) ----
    r.push_back({ "GetCamera", "Get Camera", "Rendering", false,
        {},
        { { "Position",  D::Vec3,  "", 0, "ctx->cameraPosition" },
            { "Direction", D::Vec3,  "", 0, "ctx->cameraDirection" },
            { "Up",        D::Vec3,  "", 0, "ctx->cameraUp" },
            { "Fov",       D::Float, "", 0, "ctx->cameraFovDeg" } },
        "" });

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
        { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "($0 + $1)" });

    r.push_back({ "Sub", "Subtract", "Math", false,
        { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "($0 - $1)" });

    r.push_back({ "Mul", "Multiply", "Math", false,
        { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "($0 * $1)" });

    r.push_back({ "Div", "Divide", "Math", false,
        { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "($0 / $1)" });

    r.push_back({ "Modulo", "Modulo", "Math", false,
        { { "a", D::Wildcard, "1.0f", 1 }, { "b", D::Wildcard, "1.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "($0 % $1)" });

    r.push_back({ "Sin", "Sin", "Math", false,
        { { "x", D::Float, "0.0f" } }, { { "res", D::Float, "" } },
        "sinf($0)" });

    r.push_back({ "Cos", "Cos", "Math", false,
        { { "x", D::Float, "0.0f" } }, { { "res", D::Float, "" } },
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

    // Dot/Normalize are wildcarded (Vec3 or Quat — glm::dot/normalize support both); Cross stays Vec3-only
    // (undefined for quaternions).
    r.push_back({ "DotVec3", "Dot", "Math", false,
        { { "a", D::Wildcard, "glm::vec3{ 0.0f, 0.0f, 0.0f }", 1 }, { "b", D::Wildcard, "glm::vec3{ 0.0f, 0.0f, 0.0f }", 1 } },
        { { "res", D::Float, "" } },
        "glm::dot($0, $1)" });

    r.push_back({ "CrossVec3", "Cross", "Math", false,
        { { "a", D::Vec3, "" }, { "b", D::Vec3, "" } }, { { "res", D::Vec3, "" } },
        "glm::cross($0, $1)" });

    r.push_back({ "LengthVec3", "Length", "Math", false,
        { { "vec", D::Vec3, "" } }, { { "res", D::Float, "" } },
        "glm::length($0)" });

    r.push_back({ "NormalizeVec3", "Normalize", "Math", false,
        { { "vec", D::Wildcard, "glm::vec3{ 0.0f, 0.0f, 0.0f }", 1 } },
        { { "res", D::Wildcard, "", 1 } },
        "glm::normalize($0)" });

    // ---- quaternion math ----
    r.push_back({ "MakeQuatFromEuler", "Make Quat (Euler)", "Math", false,
        { { "eulerDeg", D::Vec3, "glm::vec3{ 0.0f, 0.0f, 0.0f }" } }, { { "quat", D::Quat, "" } },
        "glm::quat(glm::radians($0))" });

    r.push_back({ "QuatToEuler", "Quat To Euler", "Math", false,
        { { "quat", D::Quat, "" } }, { { "eulerDeg", D::Vec3, "" } },
        "glm::degrees(glm::eulerAngles($0))" });

    r.push_back({ "QuatFromAxisAngle", "Quat From Axis Angle", "Math", false,
        { { "axis", D::Vec3, "glm::vec3{ 0.0f, 1.0f, 0.0f }" }, { "angleDeg", D::Float, "0.0f" } },
        { { "quat", D::Quat, "" } },
        "glm::angleAxis(glm::radians($1), $0)" });

    r.push_back({ "MulQuat", "Multiply Quat", "Math", false,
        { { "a", D::Quat, "" }, { "b", D::Quat, "" } }, { { "res", D::Quat, "" } },
        "($0 * $1)" });

    r.push_back({ "RotateVec3ByQuat", "Rotate Vec3 By Quat", "Math", false,
        { { "quat", D::Quat, "" }, { "vec", D::Vec3, "" } }, { { "res", D::Vec3, "" } },
        "($0 * $1)" });

    r.push_back({ "InverseQuat", "Inverse Quat", "Math", false,
        { { "quat", D::Quat, "" } }, { { "res", D::Quat, "" } },
        "glm::inverse($0)" });

    r.push_back({ "SlerpQuat", "Slerp Quat", "Math", false,
        { { "a", D::Quat, "" }, { "b", D::Quat, "" }, { "t", D::Float, "0.5f" } }, { { "res", D::Quat, "" } },
        "glm::slerp($0, $1, $2)" });

    // ---- generic math (wildcard: works with Float/Int/Vec3, whatever the connected type resolves to) ----
    r.push_back({ "Min", "Min", "Math", false,
        { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "glm::min($0, $1)" });

    r.push_back({ "Max", "Max", "Math", false,
        { { "a", D::Wildcard, "0.0f", 1 }, { "b", D::Wildcard, "0.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "glm::max($0, $1)" });

    r.push_back({ "Clamp", "Clamp", "Math", false,
        { { "x", D::Wildcard, "0.0f", 1 }, { "min", D::Wildcard, "0.0f", 1 }, { "max", D::Wildcard, "1.0f", 1 } },
        { { "res", D::Wildcard, "", 1 } },
        "glm::clamp($0, $1, $2)" });

    r.push_back({ "Smoothstep", "Smoothstep", "Math", false,
        { { "edge0", D::Wildcard, "0.0f", 1 }, { "edge1", D::Wildcard, "1.0f", 1 }, { "x", D::Wildcard, "0.5f", 1 } },
        { { "res", D::Wildcard, "", 1 } },
        "glm::smoothstep($0, $1, $2)" });

    r.push_back({ "Ceil", "Ceil", "Math", false,
        { { "x", D::Wildcard, "0.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "glm::ceil($0)" });

    r.push_back({ "Floor", "Floor", "Math", false,
        { { "x", D::Wildcard, "0.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "glm::floor($0)" });

    r.push_back({ "Round", "Round", "Math", false,
        { { "x", D::Wildcard, "0.0f", 1 } }, { { "res", D::Wildcard, "", 1 } },
        "glm::round($0)" });

    // ---- entity (defaults to the script's owning entity, `self`, but any node reachable through an Entity
    // pin can be targeted instead) ----
    // Position/Scale/Rotation read straight off the Entity mirror ($0->pos/scale/rot) instead of going
    // through the ABI; Rotation/Forward/Right/Up derive from $0->rot with glm:: helpers (see ScriptAPI.h).
    r.push_back({ "GetEntity", "Get Entity", "Entity", false,
        { { "Entity", D::Entity, "self" } },
        { { "Position",  D::Vec3,   "", 0, "$0->pos" },
            { "Scale",     D::Float,  "", 0, "$0->scale" },
            { "Rotation",  D::Vec3,   "", 0, "glm::degrees(glm::eulerAngles($0->rot))" },
            { "Forward",   D::Vec3,   "", 0, "($0->rot * glm::vec3(0.0f, 0.0f, -1.0f))" },
            { "Right",     D::Vec3,   "", 0, "($0->rot * glm::vec3(1.0f, 0.0f, 0.0f))" },
            { "Up",        D::Vec3,   "", 0, "($0->rot * glm::vec3(0.0f, 1.0f, 0.0f))" },
            { "Name",      D::String, "", 0, "ctx->entityGetName($0)" },
            { "Enabled",   D::Bool,   "", 0, "(ctx->entityGetEnabled($0) != 0)" },
            { "Children",  D::Int,    "", 0, "ctx->entityGetChildCount($0)" },
            { "Bounds R",  D::Float,  "", 0, "ctx->entityGetBoundsRadius($0)" } },
        "" });

    // Set Entity: writes only the inputs you actually connect (the ?k{...} conditional blocks). Position/
    // Scale/Rotation assign straight into the Entity mirror; Rotation converts degrees -> quat with glm::.
    r.push_back({ "SetEntity", "Set Entity", "Entity", true,
        { { "", D::Exec, "" },
            { "Entity",   D::Entity, "self" },
            { "Position", D::Vec3,  "glm::vec3{ 0.0f, 0.0f, 0.0f }" },
            { "Scale",    D::Float, "1.0f" },
            { "Rotation", D::Vec3,  "glm::vec3{ 0.0f, 0.0f, 0.0f }" },
            { "Enabled",  D::Bool,  "true" } },
        { { "", D::Exec, "" } },
        "?2{$1->pos = $2;\n}?3{$1->scale = $3;\n}"
        "?4{$1->rot = glm::quat(glm::radians($4));\n}?5{ctx->entitySetEnabled($1, $5);\n}#0" });

    // Spawn Entity: spawns an asset/prefab at a world position immediately and returns it, so it can be used
    // right away (e.g. AddChild it, or SetEntity its transform) in the same exec chain. Not targeted at an
    // existing entity, so it has no Entity input.
    r.push_back({ "SpawnEntity", "Spawn Entity", "Entity", true,
        { { "", D::Exec, "" },
            { "asset",    D::String, "Entities/character.pre" },
            { "position", D::Vec3,   "glm::vec3{ 0.0f, 0.0f, 0.0f }" } },
        { { "", D::Exec, "" }, { "Entity", D::Entity, "", 0, "spawned@" } },
        "Entity* spawned@ = ctx->spawnEntity($1, $2);\n#0" });

    // Destroy Entity: queues the given entity (self by default) for removal.
    r.push_back({ "DestroyEntity", "Destroy Entity", "Entity", true,
        { { "", D::Exec, "" }, { "Entity", D::Entity, "self" } },
        { { "", D::Exec, "" } },
        "ctx->destroyEntity($1);\n#0" });

    r.push_back({ "SetAnimFloat", "Set Anim Float", "Entity", true,
        { { "", D::Exec, "" }, { "Entity", D::Entity, "self" }, { "param", D::String, "speed" }, { "val", D::Float, "0.0f" } },
        { { "", D::Exec, "" } },
        "ctx->entitySetAnimFloat($1, $2, $3);\n#0" });

    r.push_back({ "SetAnimTrigger", "Set Anim Trigger", "Entity", true,
        { { "", D::Exec, "" }, { "Entity", D::Entity, "self" }, { "param", D::String, "attack" } },
        { { "", D::Exec, "" } },
        "ctx->entitySetAnimTrigger($1, $2);\n#0" });

    // Get Child Entity: looks up a direct child by name on the given entity (self by default) and branches
    // depending on whether it exists; the found child is exposed as an Entity output only valid on the Found
    // branch (see the emitted `child@` local).
    r.push_back({ "GetChildEntity", "Get Child Entity", "Entity", true,
        { { "", D::Exec, "" }, { "Parent", D::Entity, "self" }, { "Name", D::String, "" } },
        { { "Found", D::Exec, "" }, { "Not Found", D::Exec, "" }, { "Child", D::Entity, "", 0, "child@" } },
        "Entity* child@ = ctx->entityFindChild($1, $2);\n"
        "if (child@)\n{\n" + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\n"
        "else\n{\n" + std::string(1, INDENT_UP) + "#1" + std::string(1, INDENT_DOWN) + "}\n" });

    // Add Child: reparents Child under Parent (both self by default -- connect real values before using).
    r.push_back({ "AddChild", "Add Child", "Entity", true,
        { { "", D::Exec, "" }, { "Parent", D::Entity, "self" }, { "Child", D::Entity, "self" } },
        { { "", D::Exec, "" } },
        "ctx->entityAddChild($1, $2);\n#0" });

    // Remove Child: detaches Child from Parent, making it a root entity (no-op if Child isn't Parent's).
    r.push_back({ "RemoveChild", "Remove Child", "Entity", true,
        { { "", D::Exec, "" }, { "Parent", D::Entity, "self" }, { "Child", D::Entity, "self" } },
        { { "", D::Exec, "" } },
        "ctx->entityRemoveChild($1, $2);\n#0" });

    // Remove Child At Index: same as Remove Child, but looks the child up by index instead of by handle.
    r.push_back({ "RemoveChildIdx", "Remove Child At Index", "Entity", true,
        { { "", D::Exec, "" }, { "Parent", D::Entity, "self" }, { "Index", D::Int, "0" } },
        { { "", D::Exec, "" } },
        "ctx->entityRemoveChildAt($1, $2);\n#0" });

    // ---- physics (target the entity's PhysicsComponent body; entities without one no-op / read zero) ----
    r.push_back({ "GetPhysics", "Get Physics", "Physics", false,
        { { "Entity", D::Entity, "self" } },
        { { "Velocity", D::Vec3,  "", 0, "ctx->entityGetVelocity($0)" },
            { "Speed",    D::Float, "", 0, "glm::length(ctx->entityGetVelocity($0))" },
            { "Has Body", D::Bool,  "", 0, "(ctx->entityHasPhysics($0) != 0)" },
            { "Awake",    D::Bool,  "", 0, "(ctx->entityIsPhysicsAwake($0) != 0)" } },
        "" });

    r.push_back({ "SetVelocity", "Set Velocity", "Physics", true,
        { { "", D::Exec, "" }, { "Entity", D::Entity, "self" }, { "velocity", D::Vec3, "glm::vec3{ 0.0f, 0.0f, 0.0f }" } },
        { { "", D::Exec, "" } },
        "ctx->entitySetVelocity($1, $2);\n#0" });

    r.push_back({ "ApplyImpulse", "Apply Impulse", "Physics", true,
        { { "", D::Exec, "" }, { "Entity", D::Entity, "self" }, { "impulse", D::Vec3, "glm::vec3{ 0.0f, 0.0f, 0.0f }" } },
        { { "", D::Exec, "" } },
        "ctx->entityApplyImpulse($1, $2);\n#0" });

    // Teleport Body: moves a DYNAMIC body directly (a dynamic body overwrites the entity transform every
    // frame, so Set Entity has no lasting effect on it). Kinematic/static bodies follow the entity, so
    // move those with Set Entity instead; on them this node is a no-op.
    r.push_back({ "TeleportBody", "Teleport Body", "Physics", true,
        { { "", D::Exec, "" },
            { "Entity",   D::Entity, "self" },
            { "position", D::Vec3, "glm::vec3{ 0.0f, 0.0f, 0.0f }" },
            { "rotation", D::Vec3, "glm::vec3{ 0.0f, 0.0f, 0.0f }" } },
        { { "", D::Exec, "" } },
        "ctx->entityTeleportPhysics($1, $2, $3);\n#0" });

    // Ray Cast: closest hit against the physics world; the hit outputs are only valid on the Hit branch.
    r.push_back({ "RayCast", "Ray Cast", "Physics", true,
        { { "", D::Exec, "" },
            { "origin",    D::Vec3,  "glm::vec3{ 0.0f, 0.0f, 0.0f }" },
            { "direction", D::Vec3,  "glm::vec3{ 0.0f, 0.0f, -1.0f }" },
            { "maxDist",   D::Float, "100.0f" } },
        { { "Hit", D::Exec, "" }, { "Miss", D::Exec, "" },
            { "Point",    D::Vec3,  "", 0, "hitPoint@" },
            { "Normal",   D::Vec3,  "", 0, "hitNormal@" },
            { "Distance", D::Float, "", 0, "(hitFraction@ * $3)" } },
        "glm::vec3 hitPoint@{ 0.0f, 0.0f, 0.0f };\nglm::vec3 hitNormal@{ 0.0f, 0.0f, 0.0f };\nfloat hitFraction@ = 0.0f;\n"
        "if (ctx->physicsRayCast($1, glm::normalize($2) * $3, &hitPoint@, &hitNormal@, &hitFraction@) != 0)\n{\n"
        + std::string(1, INDENT_UP) + "#0" + std::string(1, INDENT_DOWN) + "}\nelse\n{\n"
        + std::string(1, INDENT_UP) + "#1" + std::string(1, INDENT_DOWN) + "}\n" });

    r.push_back({ "SetGravity", "Set Gravity", "Physics", true,
        { { "", D::Exec, "" }, { "gravity", D::Vec3, "glm::vec3{ 0.0f, -9.81f, 0.0f }" } },
        { { "", D::Exec, "" } },
        "ctx->physicsSetGravity($1);\n#0" });

    r.push_back({ "GetParent", "Get Parent", "Entity", false,
        { { "Entity", D::Entity, "self" } },
        { { "Parent", D::Entity, "" } },
        "$0->parent" });

    r.push_back({ "GetChildCount", "Get Child Count", "Entity", false,
        { { "Entity", D::Entity, "self" } },
        { { "Count", D::Int, "" } },
        "ctx->entityGetChildCount($0)" });

    r.push_back({ "GetChildIdx", "Get Child At Index", "Entity", false,
        { { "Entity", D::Entity, "self" }, { "Index", D::Int, "0" } },
        { { "Child", D::Entity, "" } },
        "ctx->entityGetChildAt($0, $1)" });

    // Send Event: fires a named On Event entry on every script listening for it (matched by name at runtime).
    r.push_back({ "SendEvent", "Send Event", "Events", true,
        { { "", D::Exec, "" }, { "Event", D::String, "" } },
        { { "", D::Exec, "" } },
        "ctx->sendEvent($1);\n#0" });

    // Send Event to Entity: same, but delivered only to the given entity's script (self by default).
    r.push_back({ "SendEventToEntity", "Send Event to Entity", "Events", true,
        { { "", D::Exec, "" }, { "Entity", D::Entity, "self" }, { "Event", D::String, "" } },
        { { "", D::Exec, "" } },
        "ctx->sendEventToEntity($1, $2);\n#0" });

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
