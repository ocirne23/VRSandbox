module;

#include <cstdarg>
#include <cstddef>
#include "ScriptAPI.h"

module Entity;

import Core;
import Core.glm;
import Core.Log;
import Core.Time;
import Core.SDL;
import Core.Sphere;
import Core.Transform;
import Core.Camera;
import RendererVK;
import Animation;
import Physics;
import Force;
import Spatial;
import Script;
import :Entity;
import :Component;
import :World;

void registerScriptDslBindings()
{
    ScriptBindings& bindings = Globals::scriptBindings;
    using T = DSLType;

    // The vector/quaternion surface. Every emit is a plain glm call, so the generated C++ stays exactly what
    // the same expression would be written by hand -- and everything here resolves out of the two headers
    // ScriptAPI.h already includes (<glm/glm.hpp> and <glm/gtc/quaternion.hpp>), so script DLLs need nothing
    // extra. "$r" (the receiver) is parenthesized wherever it lands inside a larger expression, since it can be
    // an arbitrary composed chain. T::ThisBinding means "this struct's own type", so the shared rows below read
    // identically for vec2/3/4 (see DSLType::ThisBinding).
    //
    // Registration ORDER carries the cross-type dependencies: a function's parameter/return types must already
    // exist, so vec2 comes first (vec3.xy needs it), then vec3 (vec4.xyz, and every quat function that speaks
    // vec3), then vec4, then quat. That's also why rotating a vector lives on quat as rotate(vec3) rather than
    // on vec3 as rotatedBy(quat) -- the latter would need quat before quat exists.
    const DSLType vec2 = bindings.registerStruct({ "vec2", "glm::vec2", { { "x", T::Float }, { "y", T::Float } }, "glm::vec2($1, $2)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
        },
        {
            { "length",          T::Float,       {},                                                   "glm::length($r)" },
            // Comparing or sorting by distance rarely needs the square root -- glm::length2 lives in an
            // extension header that isn't included, so the dot product with itself stands in for it.
            { "lengthSquared",   T::Float,       {},                                                   "glm::dot($r, $r)" },
            { "normalized",      T::ThisBinding, {},                                                   "glm::normalize($r)" },
            { "dot",             T::Float,       { { "other", T::ThisBinding } },                      "glm::dot($r, $1)" },
            { "distance",        T::Float,       { { "other", T::ThisBinding } },                      "glm::distance($r, $1)" },
            { "distanceSquared", T::Float,       { { "other", T::ThisBinding } },                      "glm::dot(($r) - ($1), ($r) - ($1))" },
            // Scaling by a scalar can't be written as an operator: a chain constrains every term to one type.
            { "scaled",          T::ThisBinding, { { "factor", T::Float } },                           "(($r) * ($1))" },
            { "negated",         T::ThisBinding, {},                                                   "(-($r))" },
            { "lerp",            T::ThisBinding, { { "target", T::ThisBinding }, { "t", T::Float } },  "glm::mix($r, $1, $2)" },
            { "min",             T::ThisBinding, { { "other", T::ThisBinding } },                      "glm::min($r, $1)" },
            { "max",             T::ThisBinding, { { "other", T::ThisBinding } },                      "glm::max($r, $1)" },
            { "clamp",           T::ThisBinding, { { "low", T::ThisBinding }, { "high", T::ThisBinding } }, "glm::clamp($r, $1, $2)" },
            { "abs",             T::ThisBinding, {},                                                   "glm::abs($r)" },
            { "floor",           T::ThisBinding, {},                                                   "glm::floor($r)" },
            { "ceil",            T::ThisBinding, {},                                                   "glm::ceil($r)" },
            { "round",           T::ThisBinding, {},                                                   "glm::round($r)" },
            // 2D only: the heading of the vector, and the 90-degree turn of it.
            { "angleDeg",        T::Float,       {},                                                   "glm::degrees(glm::atan(($r).y, ($r).x))" },
            { "perpendicular",   T::ThisBinding, {},                                                   "glm::vec2(-($r).y, ($r).x)" },
        } });
    const DSLType vec3 = bindings.registerStruct({ "vec3", "glm::vec3", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float } }, "glm::vec3($1, $2, $3)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
            { "z", T::Float, "$r.z" },
        },
        {
            { "length",          T::Float,       {},                                                   "glm::length($r)" },
            { "lengthSquared",   T::Float,       {},                                                   "glm::dot($r, $r)" },
            { "normalized",      T::ThisBinding, {},                                                   "glm::normalize($r)" },
            { "dot",             T::Float,       { { "other", T::ThisBinding } },                      "glm::dot($r, $1)" },
            { "cross",           T::ThisBinding, { { "other", T::ThisBinding } },                      "glm::cross($r, $1)" },
            { "distance",        T::Float,       { { "other", T::ThisBinding } },                      "glm::distance($r, $1)" },
            { "distanceSquared", T::Float,       { { "other", T::ThisBinding } },                      "glm::dot(($r) - ($1), ($r) - ($1))" },
            { "scaled",          T::ThisBinding, { { "factor", T::Float } },                           "(($r) * ($1))" },
            { "negated",         T::ThisBinding, {},                                                   "(-($r))" },
            { "lerp",            T::ThisBinding, { { "target", T::ThisBinding }, { "t", T::Float } },  "glm::mix($r, $1, $2)" },
            { "min",             T::ThisBinding, { { "other", T::ThisBinding } },                      "glm::min($r, $1)" },
            { "max",             T::ThisBinding, { { "other", T::ThisBinding } },                      "glm::max($r, $1)" },
            { "clamp",           T::ThisBinding, { { "low", T::ThisBinding }, { "high", T::ThisBinding } }, "glm::clamp($r, $1, $2)" },
            { "abs",             T::ThisBinding, {},                                                   "glm::abs($r)" },
            { "floor",           T::ThisBinding, {},                                                   "glm::floor($r)" },
            { "ceil",            T::ThisBinding, {},                                                   "glm::ceil($r)" },
            { "round",           T::ThisBinding, {},                                                   "glm::round($r)" },
            // `normal` is expected normalized, matching glm's own contract for these two.
            { "reflect",         T::ThisBinding, { { "normal", T::ThisBinding } },                     "glm::reflect($r, $1)" },
            { "refract",         T::ThisBinding, { { "normal", T::ThisBinding }, { "eta", T::Float } },"glm::refract($r, $1, $2)" },
            { "xy",              vec2,           {},                                                   "glm::vec2(($r).x, ($r).y)" },
        } });
    const DSLType vec4 = bindings.registerStruct({ "vec4", "glm::vec4", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float }, { "w", T::Float } }, "glm::vec4($1, $2, $3, $4)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
            { "z", T::Float, "$r.z" },
            { "w", T::Float, "$r.w" },
        },
        {
            { "length",          T::Float,       {},                                                   "glm::length($r)" },
            { "lengthSquared",   T::Float,       {},                                                   "glm::dot($r, $r)" },
            { "normalized",      T::ThisBinding, {},                                                   "glm::normalize($r)" },
            { "dot",             T::Float,       { { "other", T::ThisBinding } },                      "glm::dot($r, $1)" },
            { "distance",        T::Float,       { { "other", T::ThisBinding } },                      "glm::distance($r, $1)" },
            { "distanceSquared", T::Float,       { { "other", T::ThisBinding } },                      "glm::dot(($r) - ($1), ($r) - ($1))" },
            { "scaled",          T::ThisBinding, { { "factor", T::Float } },                           "(($r) * ($1))" },
            { "negated",         T::ThisBinding, {},                                                   "(-($r))" },
            { "lerp",            T::ThisBinding, { { "target", T::ThisBinding }, { "t", T::Float } },  "glm::mix($r, $1, $2)" },
            { "min",             T::ThisBinding, { { "other", T::ThisBinding } },                      "glm::min($r, $1)" },
            { "max",             T::ThisBinding, { { "other", T::ThisBinding } },                      "glm::max($r, $1)" },
            { "clamp",           T::ThisBinding, { { "low", T::ThisBinding }, { "high", T::ThisBinding } }, "glm::clamp($r, $1, $2)" },
            { "abs",             T::ThisBinding, {},                                                   "glm::abs($r)" },
            { "floor",           T::ThisBinding, {},                                                   "glm::floor($r)" },
            { "ceil",            T::ThisBinding, {},                                                   "glm::ceil($r)" },
            { "round",           T::ThisBinding, {},                                                   "glm::round($r)" },
            { "xy",              vec2,           {},                                                   "glm::vec2(($r).x, ($r).y)" },
            { "xyz",             vec3,           {},                                                   "glm::vec3(($r).x, ($r).y, ($r).z)" },
        } });
    (void)(vec4);
    // glm::quat's own 4-scalar constructor takes (w, x, y, z) -- opposite of vec4's (x, y, z, w) -- so the
    // positional args reorder in the emit while the DSL's own constructorParams/members stay x, y, z, w for
    // consistency. That raw form is rarely what an author wants, though: quatFromEuler/quatFromAxisAngle in the
    // Engine section below are the readable ways to build one.
    const DSLType quat = bindings.registerStruct({ "quat", "glm::quat", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float }, { "w", T::Float } }, "glm::quat($4, $1, $2, $3)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
            { "z", T::Float, "$r.z" },
            { "w", T::Float, "$r.w" },
        },
        {
            { "length",     T::Float,       {},                                                   "glm::length($r)" },
            { "normalized", T::ThisBinding, {},                                                   "glm::normalize($r)" },
            { "dot",        T::Float,       { { "other", T::ThisBinding } },                      "glm::dot($r, $1)" },
            // For a UNIT quaternion the conjugate IS the inverse and is cheaper; inverse is correct regardless.
            { "inverse",    T::ThisBinding, {},                                                   "glm::inverse($r)" },
            { "conjugate",  T::ThisBinding, {},                                                   "glm::conjugate($r)" },
            // Composition is not commutative: a.then(b) applies a first, then b.
            { "then",       T::ThisBinding, { { "next", T::ThisBinding } },                       "(($1) * ($r))" },
            { "slerp",      T::ThisBinding, { { "target", T::ThisBinding }, { "t", T::Float } },  "glm::slerp($r, $1, $2)" },
            { "rotate",     vec3,           { { "v", vec3 } },                                    "(($r) * ($1))" },
            { "euler",      vec3,           {},                                                   "glm::degrees(glm::eulerAngles($r))" },
            { "axis",       vec3,           {},                                                   "glm::axis($r)" },
            { "angleDeg",   T::Float,       {},                                                   "glm::degrees(glm::angle($r))" },
        } });
    // An Entity that may not be there ("self.parent" at the root, a spawn that failed, a query that found
    // nothing). NOT an Entity value: the only way in is "ifexist Entity e in <the Entity? expression>", which
    // makes the null check impossible to forget and leaves nothing in the DSL needing a manual one.
    //
    // The lookup is GENERIC -- "$r" is whatever expression produced the optional, so one type serves a member
    // (self.parent) and a function return (world.spawn / world.nearestEntity) with no per-source variant. Each
    // of those supplies its own emit; this only tests it, once.
    const DSLType optionalEntityType = bindings.registerOptionalType("Entity?", T::Entity, "($v = $r) != nullptr");

    bindings.registerObject({ "self", T::Entity, /*sidebarTopLevel*/ true,
        {
            { "setEnabled",     T::Void,  { { "enabled", T::Bool } }, "ctx->entitySetEnabled($r, $1)" },
        },
        {
            { "pos",     vec3,        "$r->pos" }, // self is Entity* -- a real field of the ABI mirror struct
            { "scale",   T::Float,    "$r->scale" },
            { "rot",     quat,        "$r->rot" },
            { "parent",  optionalEntityType, "$r->parent", /*writable*/ false },
            // Both belong to the SCRIPT, not to the entity: `data`'s emit ignores the receiver entirely and
            // `events` resolves against this document's own list, so neither means anything on another entity.
            { "data",    T::ScriptData,   "(*scriptData)", /*writable*/ false, /*selfOnly*/ true },
            { "events",  T::ScriptEvents, "$r",            /*writable*/ false, /*selfOnly*/ true }, // special case in transpiler
        } });

    // The child list as its own collection type, so walking it reads as "foreach Entity e in
    // self.scene.children" -- a component isn't a collection, and shouldn't be iterable as one. It SPELLS as
    // "Entity[]" (see dslTypeName): to a script it's an indexable collection like any other. Nothing is copied
    // to produce it -- the `children` member's emit is just "$r", so the value IS the SceneComponent handle and
    // these templates run straight against it. sceneGetChildAt is null-safe past the end on its own, which is
    // what keeps the loop sound if the child list changes mid-iteration AND what lets `ifexist` test the miss.
    const DSLType entityListType = bindings.registerSequenceType("EntityList", T::Entity,
        "ctx->sceneGetChildCount($r)", "ctx->sceneGetChildAt($r, $i)",
        "($v = ctx->sceneGetChildAt($r, $1)) != nullptr");

    const DSLType sceneType = bindings.registerComponentType("scene", "SceneComponent", EComponentID_Scene,
        "ctx->entityGetSceneComponent($r)");
    bindings.registerObject({ "scene", sceneType, /*sidebarTopLevel*/ false,
        {
            { "getNumChildren", T::Int,    {},                             "ctx->sceneGetChildCount($r)"},
            // First DIRECT child with this display name -- an Entity?, since there may be none. Not recursive:
            // "my child called X" is a different question from "anything under me called X".
            { "findChild",      optionalEntityType, {{ "name", T::String }},    "ctx->sceneFindChild($r, $1)"},
            { "getChild",       T::Entity, {{ "index", T::Int }},          "ctx->sceneGetChildAt($r, $1)"},
            { "removeChildIdx", T::Void,   {{ "index", T::Int }},          "ctx->sceneRemoveChildAt($r, $1)"},
            { "removeChild",    T::Void,   {{ "entity", T::Entity }},      "ctx->sceneRemoveChild($r, $1)"},
            { "addChild",       T::Void,   {{ "entity", T::Entity }},      "ctx->sceneAddChild($r, $1)"},
        },
        {
            // A MEMBER, not an accessor call: "self.scene.children" is the collection itself. Read-only -- the
            // list belongs to the engine, and add/removeChild are how it changes.
            { "children", entityListType, "$r", /*writable*/ false },
        } });

    // Animation is the ANIMATOR's surface, not the entity's: a script drives the state machine's parameters and
    // the graph's own transitions decide what plays (see Animation:StateMachine). Nothing here names a clip --
    // that's the .apl author's job, and going around it would desync the graph from what's actually playing.
    const DSLType animatorType = bindings.registerComponentType("animator", "AnimatorComponent", EComponentID_Animator,
        "ctx->entityGetAnimatorComponent($r)");
    bindings.registerObject({ "animator", animatorType, /*sidebarTopLevel*/ false,
        {
            { "setFloat",   T::Void,  { { "name", T::String }, { "value", T::Float } }, "ctx->animatorSetFloat($r, $1, $2)" },
            { "setBool",    T::Void,  { { "name", T::String }, { "value", T::Bool } },  "ctx->animatorSetBool($r, $1, $2)" },
            // Consumed by the next transition that tests it -- fire-and-forget, so there is no getter.
            { "setTrigger", T::Void,  { { "name", T::String } },                        "ctx->animatorSetTrigger($r, $1)" },
            { "getFloat",   T::Float, { { "name", T::String } },                        "ctx->animatorGetFloat($r, $1)" },
            { "getBool",    T::Bool,  { { "name", T::String } },                        "(ctx->animatorGetBool($r, $1) != 0)" },
            // Pausing the animator without touching its parameters. A FUNCTION, not a writable member: a
            // member's emit is used verbatim as an assignment TARGET, so only a real lvalue ("$r->pos") can be
            // writable -- a getter call can't sit on the left of an `=`.
            { "setEnabled", T::Void,  { { "enabled", T::Bool } },                       "ctx->animatorSetEnabled($r, $1)" },
        },
        {
            { "state",       T::String, "ctx->animatorGetStateName($r)",   /*writable*/ false },
            { "timeInState", T::Float,  "ctx->animatorGetTimeInState($r)", /*writable*/ false },
            { "speed",       T::Float,  "ctx->animatorGetSpeed($r)",       /*writable*/ false },
            { "enabled",     T::Bool,   "(ctx->animatorGetEnabled($r) != 0)", /*writable*/ false },
        } });

    const DSLType physicsType = bindings.registerComponentType("physics", "PhysicsComponent", EComponentID_Physics,
        "ctx->entityGetPhysicsComponent($r)");
    bindings.registerObject({ "physics", physicsType, /*sidebarTopLevel*/ false,
        {
            { "getVelocity",  vec3,   {},                                              "ctx->physicsGetVelocity($r)" },
            { "setVelocity",  T::Void, { { "velocity", vec3 } },                       "ctx->physicsSetVelocity($r, $1)" },
            { "applyImpulse", T::Void, { { "impulse", vec3 } },                        "ctx->physicsApplyImpulse($r, $1)" },
            { "isAwake",      T::Bool, {},                                             "(ctx->physicsIsAwake($r) != 0)" },
            { "teleport",     T::Void, { { "position", vec3 }, { "eulerDeg", vec3 } }, "ctx->physicsTeleport($r, $1, $2)" },
        },
        {} });

    const DSLType audioType = bindings.registerComponentType("audio", "AudioComponent", EComponentID_Audio,
        "ctx->entityGetAudioComponent($r)");
    bindings.registerObject({ "audio", audioType, /*sidebarTopLevel*/ false,
        {
            { "trigger", T::Void, { { "alias", T::String } }, "ctx->audioTrigger($r, self, $1, 0, glm::vec3(0.0f), 1.0f, 1.0f)" },
            { "stop",    T::Void, { { "alias", T::String } }, "ctx->audioStop($r, $1)" },
        },
        {} });

    const DSLType forceType = bindings.registerComponentType("force", "ForceComponent", EComponentID_Force,
        "ctx->entityGetForceComponent($r)");
    bindings.registerObject({ "force", forceType, /*sidebarTopLevel*/ false,
        {
            { "getOutput",   T::Float, {},                          "ctx->forceGetOutput($r)" },
            { "setOutput",   T::Void,  { { "output", T::Float } },  "ctx->forceSetOutput($r, $1)" },
            { "getReach",    T::Float, {},                          "ctx->forceGetReach($r)" },
            { "setReach",    T::Void,  { { "reach", T::Float } },   "ctx->forceSetReach($r, $1)" },
            { "setTeam",     T::Void,  { { "team", T::Int } },      "ctx->forceSetTeam($r, $1)" },
            { "getPressure", T::Float, {},                          "ctx->forceGetPressure($r)" },
        },
        {} });
    // free functions
    // A raycast result. Registered as a plain struct so its fields read normally once a hit is proven, and
    // wrapped in an optional so proving it is the only way in: "ifexist RayHit h in world.rayCast(...)". The
    // constructor takes no arguments -- a RayHit is something the engine hands back, never something a script
    // builds -- and every field is read-only for the same reason.
    const DSLType rayHitType = bindings.registerStruct({ "RayHit", "VrRayHit", {}, "VrRayHit()",
        {
            { "point",    vec3,     "$r.point",    /*writable*/ false },
            { "normal",   vec3,     "$r.normal",   /*writable*/ false },
            { "distance", T::Float, "$r.distance", /*writable*/ false },
        },
        {} });
    const DSLType optionalRayHitType = bindings.registerOptionalType("OptionalRayHit", rayHitType,
        "(($v = $r).hit != 0)");

    // Everything mathematical lives under one prefix, so "math." is the single thing to type to see what is
    // available. A NAMESPACE, not an object with state: its functions require a receiver like any binding
    // object's (which is what makes `sin(x)` unauthorable and `math.sin(x)` the only spelling), but none of
    // their emits mention "$r", so the generated C++ is a direct glm/std call with no receiver in sight.
    //
    // ANGLES ARE DEGREES across the whole DSL surface (quatFromEuler, quat.angleDeg, and these) -- the
    // conversion lives in the emit, so a script never handles radians unless it asks for them by name.
    const DSLType mathType = bindings.registerNamespace("math");
    bindings.registerObject({ "math", mathType, /*sidebarTopLevel*/ true,
        {
            // -- basics --
            { "abs",        T::Float, { { "x", T::Float } },                                        "glm::abs($1)" },
            { "sign",       T::Float, { { "x", T::Float } },                                        "glm::sign($1)" },
            { "min",        T::Float, { { "a", T::Float }, { "b", T::Float } },                     "glm::min($1, $2)" },
            { "max",        T::Float, { { "a", T::Float }, { "b", T::Float } },                     "glm::max($1, $2)" },
            { "clamp",      T::Float, { { "x", T::Float }, { "low", T::Float }, { "high", T::Float } }, "glm::clamp($1, $2, $3)" },
            { "saturate",   T::Float, { { "x", T::Float } },                                        "glm::clamp($1, 0.0f, 1.0f)" },
            { "sqrt",       T::Float, { { "x", T::Float } },                                        "glm::sqrt($1)" },
            { "pow",        T::Float, { { "base", T::Float }, { "exponent", T::Float } },           "glm::pow($1, $2)" },
            { "exp",        T::Float, { { "x", T::Float } },                                        "glm::exp($1)" },
            { "log",        T::Float, { { "x", T::Float } },                                        "glm::log($1)" },
            { "log2",       T::Float, { { "x", T::Float } },                                        "glm::log2($1)" },
            // Sign follows the DIVIDEND (C fmod semantics), so math.mod(-1, 4) is -1, not 3.
            { "mod",        T::Float, { { "x", T::Float }, { "y", T::Float } },                     "std::fmod($1, $2)" },

            // -- rounding --
            { "floor",      T::Float, { { "x", T::Float } },                                        "glm::floor($1)" },
            { "ceil",       T::Float, { { "x", T::Float } },                                        "glm::ceil($1)" },
            { "round",      T::Float, { { "x", T::Float } },                                        "glm::round($1)" },
            { "trunc",      T::Float, { { "x", T::Float } },                                        "glm::trunc($1)" },
            { "fract",      T::Float, { { "x", T::Float } },                                        "glm::fract($1)" },
            { "toInt",      T::Int,   { { "x", T::Float } },                                        "((int)($1))" }, // truncates, like C++

            // -- interpolation / ranges --
            { "lerp",         T::Float, { { "a", T::Float }, { "b", T::Float }, { "t", T::Float } }, "glm::mix($1, $2, $3)" },
            // 0 when the input range is empty, rather than a division by zero.
            { "inverseLerp",  T::Float, { { "a", T::Float }, { "b", T::Float }, { "value", T::Float } }, "vrInverseLerp($1, $2, $3)" },
            { "remap",        T::Float, { { "value", T::Float }, { "inMin", T::Float }, { "inMax", T::Float }, { "outMin", T::Float }, { "outMax", T::Float } }, "vrRemap($1, $2, $3, $4, $5)" },
            { "step",         T::Float, { { "edge", T::Float }, { "x", T::Float } },                 "glm::step($1, $2)" },
            { "smoothstep",   T::Float, { { "edge0", T::Float }, { "edge1", T::Float }, { "x", T::Float } }, "glm::smoothstep($1, $2, $3)" },
            // Frame-rate independent approach: never overshoots the target.
            { "moveTowards",  T::Float, { { "current", T::Float }, { "target", T::Float }, { "maxDelta", T::Float } }, "vrMoveTowards($1, $2, $3)" },

            // -- trigonometry (degrees in, degrees out) --
            { "sin",        T::Float, { { "angleDeg", T::Float } },                                 "glm::sin(glm::radians($1))" },
            { "cos",        T::Float, { { "angleDeg", T::Float } },                                 "glm::cos(glm::radians($1))" },
            { "tan",        T::Float, { { "angleDeg", T::Float } },                                 "glm::tan(glm::radians($1))" },
            { "asin",       T::Float, { { "x", T::Float } },                                        "glm::degrees(glm::asin(glm::clamp($1, -1.0f, 1.0f)))" },
            { "acos",       T::Float, { { "x", T::Float } },                                        "glm::degrees(glm::acos(glm::clamp($1, -1.0f, 1.0f)))" },
            { "atan",       T::Float, { { "x", T::Float } },                                        "glm::degrees(glm::atan($1))" },
            { "atan2",      T::Float, { { "y", T::Float }, { "x", T::Float } },                     "glm::degrees(glm::atan($1, $2))" },
            // For the rare case a raw radian value is wanted (feeding a shader constant, say).
            { "toRadians",  T::Float, { { "degrees", T::Float } },                                  "glm::radians($1)" },
            { "toDegrees",  T::Float, { { "radians", T::Float } },                                  "glm::degrees($1)" },

            // -- angles -- the wrap-around cases hand-rolled arithmetic gets wrong
            { "wrapAngle",        T::Float, { { "angleDeg", T::Float } },                           "vrWrapAngle($1)" },      // -> [-180, 180]
            { "deltaAngle",       T::Float, { { "fromDeg", T::Float }, { "toDeg", T::Float } },     "vrDeltaAngle($1, $2)" }, // the short way around
            { "lerpAngle",        T::Float, { { "a", T::Float }, { "b", T::Float }, { "t", T::Float } }, "vrLerpAngle($1, $2, $3)" },
            { "moveTowardsAngle", T::Float, { { "currentDeg", T::Float }, { "targetDeg", T::Float }, { "maxDelta", T::Float } }, "vrMoveTowardsAngle($1, $2, $3)" },

            // -- randomness -- one engine-side stream, so a hot-reload does not restart the sequence
            { "randomFloat",      T::Float, { { "min", T::Float }, { "max", T::Float } },           "ctx->randomFloat($1, $2)" },
            { "randomInt",        T::Int,   { { "min", T::Int }, { "max", T::Int } },               "ctx->randomInt($1, $2)" }, // both ends inclusive
            { "randomUnitVector", vec3,     {},                                                     "vrRandomUnitVector(ctx)" },

            // -- rotations -- the constructors that used to sit at top level
            { "quatFromEuler",     quat,   { { "eulerDeg", vec3 } },                                "glm::quat(glm::radians($1))" },
            { "quatFromAxisAngle", quat,   { { "axis", vec3 }, { "angleDeg", T::Float } },          "glm::angleAxis(glm::radians($2), glm::normalize($1))" },
            { "quatIdentity",      quat,   {},                                                      "glm::quat(1.0f, 0.0f, 0.0f, 0.0f)" },
            // The rotation whose -Z faces `forward` (the engine forward axis) with +Y as close to `up` as possible.
            { "quatLookRotation",  quat,   { { "forward", vec3 }, { "up", vec3 } },                 "glm::quatLookAt(glm::normalize($1), glm::normalize($2))" },
        },
        {
            { "pi",      T::Float, "3.14159265358979f", /*writable*/ false },
            { "tau",     T::Float, "6.28318530717959f", /*writable*/ false },
            { "epsilon", T::Float, "1.0e-6f",           /*writable*/ false },
        } });

    // The render camera, as a read-only view onto the per-frame fields ScriptContext already carries (see its
    // `Per-frame data` block -- ScriptContext::update refreshes them once a frame). Every emit here reads a
    // FIELD, not a call, so none of them mention "$r": the object exists purely to group them under a name.
    const DSLType cameraType = bindings.registerNamespace("Camera");
    bindings.registerObject({ "Camera", cameraType, /*sidebarTopLevel*/ false,
        {},
        {
            { "position",  vec3,     "ctx->cameraPosition",  /*writable*/ false },
            { "direction", vec3,     "ctx->cameraDirection", /*writable*/ false }, // normalized, the way it looks
            { "up",        vec3,     "ctx->cameraUp",        /*writable*/ false },
            { "right",     vec3,     "glm::normalize(glm::cross(ctx->cameraDirection, ctx->cameraUp))", /*writable*/ false },
            { "fovDeg",    T::Float, "ctx->cameraFovDeg",    /*writable*/ false },
            { "nearPlane", T::Float, "ctx->cameraNear",      /*writable*/ false },
            { "farPlane",  T::Float, "ctx->cameraFar",       /*writable*/ false },
        } });

    // The ambient state a script ACTS ON rather than owns: spawning, the physics world, the sky, global events.
    // Everything an entity owns stays on self/its components -- the split is "whose state is this", which is
    // also why nothing here takes an entity as a receiver.
    bindings.registerObject({ "world", T::World, /*sidebarTopLevel*/ true,
        {
            // -- entities -- spawn returns an Entity?: a bad path or a failed spawn has no entity to give back,
            // and the DSL has no null, so the caller proves it with `ifexist` like any other optional.
            { "spawn",         optionalEntityType, { { "assetPath", T::String }, { "position", vec3 } }, "ctx->spawnEntity($1, $2)" },
            { "destroy",       T::Void,  { { "entity", T::Entity } },                                    "ctx->destroyEntity($1)" },
            // By display name, ROOTS only -- names are not unique, so this is "the first one", and a
            // whole-scene search would be a different and far more expensive operation.
            { "findRootEntity", optionalEntityType, { { "displayName", T::String } },              "ctx->worldFindRootEntity($1)" },
            // Nearest OTHER entity within the radius (pass self as `exclude` to skip yourself).
            { "nearestEntity", optionalEntityType, { { "position", vec3 }, { "maxRadius", T::Float }, { "exclude", T::Entity } }, "ctx->spatialGetNearestEntity($1, $2, $3)" },

            // -- raycasts -- the full hit is an optional, so point/normal/distance can only be read once the
            // hit is proven; rayCastDistance is the cheap form when only "how far to the wall" matters (it
            // reports maxDistance itself on a miss, never a sentinel outside the query range).
            { "rayCast",         optionalRayHitType, { { "origin", vec3 }, { "direction", vec3 }, { "maxDistance", T::Float } }, "ctx->worldRayCast($1, $2, $3)" },
            { "rayCastDistance", T::Float, { { "origin", vec3 }, { "direction", vec3 }, { "maxDistance", T::Float } },           "ctx->physicsRayCastDistance($1, $2, $3)" },

            // -- events -- global, or delivered to one entity's own script
            { "sendEvent",   T::Void, { { "eventName", T::String } },                              "ctx->sendEvent($1)" },
            { "sendEventTo", T::Void, { { "entity", T::Entity }, { "eventName", T::String } },     "ctx->sendEventToEntity($1, $2)" },

            // -- sky / lighting --
            { "setSun",          T::Void, { { "direction", vec3 }, { "color", vec3 }, { "intensity", T::Float } },                      "ctx->setSun($1, $2, $3)" },
            { "spawnPointLight", T::Void, { { "position", vec3 }, { "range", T::Float }, { "color", vec3 }, { "intensity", T::Float } }, "ctx->spawnPointLight($1, $2, $3, $4)" },

            // -- physics globals --
            { "setGravity",  T::Void, { { "gravity", vec3 } },                                     "ctx->physicsSetGravity($1)" },

            // -- debug drawing -- this frame only, so calling it every Update is how a line stays on screen
            { "drawLine",    T::Void, { { "from", vec3 }, { "to", vec3 }, { "color", vec3 } },     "ctx->worldDrawLine($1, $2, $3)" },
        },
        {
            // Normalized, pointing the way the sunlight TRAVELS (not toward the sun).
            { "sunDirection", vec3,     "ctx->worldGetSunDirection(0)", /*writable*/ false },
            // Seconds since startup -- the same clock deltaSeconds comes from, already on the context.
            { "time",         T::Float, "ctx->elapsedSeconds",          /*writable*/ false },
            { "camera",       cameraType, "ctx",                        /*writable*/ false }, // "$r" unused by its members
        } });

    bindings.registerObject({ nullptr, T::Void, /*sidebarTopLevel*/ false,
        {
            // printf-style formatting: the format string, then any number of values ("$*" expands the tail,
            // comma-prefixed, so a zero-vararg call still emits valid C++). The DSL doesn't check the format
            // against the arguments -- same "author's responsibility" stance the rest of the ABI takes.
            { "printf",          T::Void,  { { "format", T::String } },                                     "ctx->logf($1$*)", /*isPositionalCall*/ false, /*isVariadic*/ true },
            { "isKeyDown",       T::Bool,  { { "keyName", T::String } },                                     "(ctx->isKeyDown($1) != 0)" },
        },
        {} });

    // cppSuffix's trailing "ScriptData* scriptData" is the GENERATED file's own concrete type (declared earlier
    // in that same file) -- the real ABI typedefs (ScriptOnSpawnFn etc., ScriptAPI.h) still take "void*", since
    // they're shared across every script and ScriptData's layout differs per script. The host only ever calls
    // these through a reinterpret_cast to those typedefs, so the two need not textually match -- both are
    // simple pointer-sized parameters, identical at the ABI/calling-convention level.
    bindings.registerEntryPoint({ "OnSpawn",         {},                                            ", ScriptData* scriptData", "REGISTER_ON_SPAWN()" });
    bindings.registerEntryPoint({ "OnDestroy",       {},                                            ", ScriptData* scriptData", "REGISTER_ON_DESTROY()" });
    bindings.registerEntryPoint({ "Update",          { { "deltaSeconds", T::Float } },              ", float deltaSeconds, ScriptData* scriptData", "REGISTER_UPDATE()" });
    bindings.registerEntryPoint({ "OnEvent",         { { "eventIdx", T::Int } },                    ", int eventIdx, ScriptData* scriptData", "REGISTER_ON_EVENT()" });
    bindings.registerEntryPoint({ "OnPhysicsEvent",  { { "begin", T::Int }, { "sensor", T::Int } }, ", Entity* other, int begin, int sensor, long long contactId, ScriptData* scriptData", "REGISTER_ON_PHYSICS_EVENT()" });
}

// ---- engine-owned script arrays (the DSL's `T[]`, see VrArray in ScriptAPI.h) ----------------------------
// Storage lives HERE, not in the script's ScriptData block, which holds only the handle. That's what makes a
// hot-reload free: the block survives (same layout id), so the handles survive, so the contents survive with
// nothing to copy. It's also what keeps a script from ever owning heap memory across a DLL swap.
//
// A handle is {index, generation}: freeing bumps the slot's generation, so a stale or garbage id -- e.g. a
// ScriptData field reinterpreted after a layout change slipped through -- fails lookup and every operation
// degrades to a no-op or a default. That, plus each accessor's own range check, is the memory-safety guarantee: no
// value a script can put in a handle field can make the engine touch memory it doesn't own.
namespace
{
    struct ScriptArray
    {
        std::vector<uint8> bytes;        // elements packed raw; elemSize is fixed at first push
        std::vector<EntityPtr> entities; // Entity elements instead: refcounted, so a destroyed entity reads back
                                          // as null rather than dangling (bytes stays empty for these)
        std::vector<std::string> strings; // string elements: copied engine-side, so they don't point into a
                                           // script DLL's .rdata and dangle when it unloads (bytes stays empty)
        int elemSize = 0;
        int elemKind = 0;                // the DSLType value -- decides which of the three stores is in use
        uint16 generation = 1;           // bumped on free; 0 is never a live generation, so handle 0 is always invalid
        bool live = false;
        ScriptComponent* owner = nullptr;
    };

    std::vector<ScriptArray> g_scriptArrays;
    std::vector<uint32> g_freeScriptArrays;

    constexpr int kEntityElemKind = static_cast<int>(DSLType::Entity);
    constexpr int kStringElemKind = static_cast<int>(DSLType::String);

    constexpr uint32 makeArrayHandle(uint32 index, uint16 generation) { return (index + 1u) | (uint32(generation) << 16); }
    constexpr uint32 arrayHandleIndex(uint32 handle) { return (handle & 0xFFFFu) - 1u; }
    constexpr uint16 arrayHandleGeneration(uint32 handle) { return uint16(handle >> 16); }

    // The ONLY way a handle becomes a ScriptArray -- everything below goes through it, so a bad id can never
    // reach the stores.
    ScriptArray* resolveScriptArray(uint32 handle)
    {
        if (handle == 0)
            return nullptr;
        const uint32 index = arrayHandleIndex(handle);
        if (index >= g_scriptArrays.size())
            return nullptr;
        ScriptArray& array = g_scriptArrays[index];
        if (!array.live || array.generation != arrayHandleGeneration(handle))
            return nullptr;
        return &array;
    }

    int scriptArrayCount(const ScriptArray& array)
    {
        if (array.elemKind == kEntityElemKind)
            return static_cast<int>(array.entities.size());
        if (array.elemKind == kStringElemKind)
            return static_cast<int>(array.strings.size());
        return array.elemSize > 0 ? static_cast<int>(array.bytes.size() / array.elemSize) : 0;
    }
}

// Frees every array `owner` created and forgets their handles -- called when the component dies, and when its
// data block is discarded (a layout change), which is the moment those handles stop being reachable.
void releaseScriptArrays(ScriptComponent& owner)
{
    for (uint32 handle : owner.ownedArrays)
        if (ScriptArray* array = resolveScriptArray(handle); array != nullptr)
        {
            array->live = false;
            ++array->generation; // any surviving copy of this handle is now stale, and resolves to nothing
            array->bytes.clear();
            array->bytes.shrink_to_fit();
            array->entities.clear();
            array->strings.clear();
            array->owner = nullptr;
            g_freeScriptArrays.push_back(arrayHandleIndex(handle));
        }
    owner.ownedArrays.clear();
}

#pragma warning(push)
#pragma warning(disable: 4190) // for glm types
extern "C" // The thunks have C linkage (external) so the cooked App-Scripts can call them
{
    // ---- string literals ----
    const char* thunk_internString(const char* text)
    {
        if (text == nullptr)
            return "";
        // unordered_set is node-based, so an element's address is stable for the life of the set even across
        // rehashing -- which is exactly what makes the returned pointer safe to keep forever. Never erased:
        // the set only ever grows to the number of distinct literals across all loaded scripts.
        static std::unordered_set<std::string> interned;
        return interned.emplace(text).first->c_str();
    }

    // ---- script arrays ----
    void thunk_arrayPush(Entity* owner, VrArray* handle, int elemKind, int elemSize, const void* value)
    {
        if (handle == nullptr || owner == nullptr || elemSize <= 0 || value == nullptr)
            return;
        ScriptArray* array = resolveScriptArray(*handle);
        if (array == nullptr)
        {
            // Either the first push (handle 0, from a zeroed data block) or a handle that no longer resolves;
            // both mean "this field has no array", so make one and write its id back through the field.
            ScriptComponent* component = getComponent<ScriptComponent>(owner);
            if (component == nullptr)
                return;
            uint32 index;
            if (!g_freeScriptArrays.empty())
            {
                index = g_freeScriptArrays.back();
                g_freeScriptArrays.pop_back();
            }
            else
            {
                index = static_cast<uint32>(g_scriptArrays.size());
                g_scriptArrays.emplace_back();
            }
            array = &g_scriptArrays[index];
            array->live = true;
            array->elemSize = elemSize;
            array->elemKind = elemKind;
            array->owner = component;
            *handle = makeArrayHandle(index, array->generation);
            component->ownedArrays.push_back(*handle);
        }
        // The caller's bytes are its OWN representation (Entity* / const char* / a POD value); the two non-POD
        // kinds convert into engine-owned storage here rather than being stored as given.
        if (array->elemKind == kEntityElemKind)
            array->entities.emplace_back(*static_cast<Entity* const*>(value));
        else if (array->elemKind == kStringElemKind)
        {
            const char* text = *static_cast<const char* const*>(value);
            array->strings.emplace_back(text != nullptr ? text : "");
        }
        else
        {
            array->bytes.resize(array->bytes.size() + array->elemSize);
            std::memcpy(array->bytes.data() + array->bytes.size() - array->elemSize, value, array->elemSize);
        }
    }

    int thunk_arrayGet(VrArray handle, int index, int elemSize, void* outValue)
    {
        ScriptArray* array = resolveScriptArray(handle);
        if (array == nullptr || outValue == nullptr || index < 0 || index >= scriptArrayCount(*array))
            return 0;
        if (array->elemKind == kEntityElemKind)
        {
            // A refcounted element whose entity has since died reports a MISS, exactly like an out-of-range
            // index -- the DSL has no null Entity to hand back, and `ifexist` is the only way to read one, so
            // "the slot is there but its entity is gone" and "the slot isn't there" are the same answer.
            Entity* entity = array->entities[index].get();
            if (entity == nullptr)
                return 0;
            *static_cast<Entity**>(outValue) = entity;
            return 1;
        }
        if (array->elemKind == kStringElemKind)
        {
            // Points into engine-owned storage, valid until this array is next mutated -- the same contract
            // entityGetName already has.
            *static_cast<const char**>(outValue) = array->strings[index].c_str();
            return 1;
        }
        if (elemSize != array->elemSize)
            return 0; // the caller's T doesn't match what this array holds -- refuse rather than mis-copy
        std::memcpy(outValue, array->bytes.data() + static_cast<size_t>(index) * array->elemSize, array->elemSize);
        return 1;
    }

    void thunk_arraySet(VrArray handle, int index, int elemSize, const void* value)
    {
        ScriptArray* array = resolveScriptArray(handle);
        if (array == nullptr || value == nullptr || index < 0 || index >= scriptArrayCount(*array))
            return;
        if (array->elemKind == kEntityElemKind)
            array->entities[index] = EntityPtr(*static_cast<Entity* const*>(value));
        else if (array->elemKind == kStringElemKind)
        {
            const char* text = *static_cast<const char* const*>(value);
            array->strings[index] = text != nullptr ? text : "";
        }
        else if (elemSize == array->elemSize)
            std::memcpy(array->bytes.data() + static_cast<size_t>(index) * array->elemSize, value, array->elemSize);
    }

    int thunk_arrayCount(VrArray handle)
    {
        const ScriptArray* array = resolveScriptArray(handle);
        return array != nullptr ? scriptArrayCount(*array) : 0;
    }

    void thunk_arrayClear(VrArray handle)
    {
        if (ScriptArray* array = resolveScriptArray(handle); array != nullptr)
        {
            array->bytes.clear();
            array->entities.clear();
            array->strings.clear();
        }
    }

    // ---- context thunks -----------------------------------------------------
    void thunk_log(const char* message) { Log::info(message ? message : ""); }
    void thunk_logf(const char* message, ...) 
    {
        va_list args;
        va_start(args, message);
        int size_s = std::snprintf(nullptr, 0, message, args) + 1;
		if (size_s <= 0) { va_end(args); return; }
		size_t size = static_cast<size_t>(size_s);
		std::string formattedString(size, '\0');
		std::vsnprintf(&formattedString[0], size, message, args);
        Log::info(formattedString);
        va_end(args);
    }

    void thunk_vlogf(const char* fmt, va_list ap)
    {
        if (!fmt) return;
        va_list ap2; va_copy(ap2, ap);
        const int n = std::vsnprintf(nullptr, 0, fmt, ap2); va_end(ap2);
        if (n < 0) return;
        std::string s(static_cast<size_t>(n) + 1, '\0');
        std::vsnprintf(&s[0], s.size(), fmt, ap);
        s.resize(static_cast<size_t>(n));
        Log::info(s);
    }

    int thunk_isKeyDown(const char* keyName)
    {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        return (keys && keyName) ? (keys[scancodeFromName(keyName)] ? 1 : 0) : 0;
    }

    void thunk_spawnPointLight(glm::vec3 position, float range, glm::vec3 color, float intensity)
    {
        Globals::rendererVK.addPointLight(PointLight(position, range, color, intensity));
    }

    void thunk_setSun(glm::vec3 direction, glm::vec3 color, float intensity)
    {
        Globals::rendererVK.setSunLight(direction, color, intensity);
    }

    Entity* thunk_spawnEntity(const char* assetPath, glm::vec3 position)
    {
        if (!assetPath) return nullptr;
        EntityPtr spawned = Globals::world.spawnAssetFile(assetPath, Transform(position, 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
        if (!spawned) return nullptr;
        Entity* raw = spawned.get();
		Globals::scriptEvents.addReparentRequest(std::move(spawned), EntityPtr(nullptr));
        return raw;
    }

    void thunk_destroyEntity(Entity* e)
    {
        Globals::scriptEvents.addDestroyRequest(EntityPtr(e));
    }

    const char* thunk_entityGetName(Entity* e) { return e->getName(); }

    int thunk_entityGetEnabled(Entity* en) { return en->isEnabled() ? 1 : 0; }
    int thunk_entityGetChildCount(Entity* en)
    {
        if (SceneComponent* sc = getComponent<SceneComponent>(en))
			return (int)sc->children.size();
        return 0;
    }
    float thunk_entityGetBoundsRadius(Entity* en)
    {
		if (RenderComponent* rc = getComponent<RenderComponent>(en))
			return rc->node.getWorldBounds().radius;
        return 0.0f;
    }

    void thunk_entitySetEnabled(Entity* en, int enabled)              { en->setEnabled(enabled != 0); }
    void thunk_entitySetAnimFloat(Entity* en, const char* p, float v) { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setFloat(p, v); }
    void thunk_entitySetAnimBool(Entity * en, const char* p, int v)   { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setBool(p, v != 0); }
    void thunk_entitySetAnimTrigger(Entity* en, const char* p)        { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setTrigger(p); }

    // ---- physics thunks ----
    PhysicsComponent* physicsOf(Entity* en)
    {
        if (!en)
            return nullptr;
        PhysicsComponent* pc = getComponent<PhysicsComponent>(en);
        return (pc && pc->body.isValid()) ? pc : nullptr;
    }

    void thunk_physicsSetGravity(glm::vec3 gravity) { Globals::physics.setGravity(gravity); }

    int thunk_physicsRayCast(glm::vec3 origin, glm::vec3 translation, glm::vec3* outPoint, glm::vec3* outNormal, float* outFraction)
    {
        const PhysicsWorld::RayHit hit = Globals::physics.castRayClosest(origin, translation);
        if (outPoint)    *outPoint = hit.point;
        if (outNormal)   *outNormal = hit.normal;
        if (outFraction) *outFraction = hit.fraction;
        return hit.hit ? 1 : 0;
    }

    float thunk_physicsRayCastDistance(glm::vec3 origin, glm::vec3 dir, float maxDist)
    {
        const glm::vec3 translation = glm::normalize(dir) * maxDist;
        const PhysicsWorld::RayHit hit = Globals::physics.castRayClosest(origin, translation);
        return hit.hit ? hit.fraction * maxDist : maxDist;
    }

    int thunk_physicsContactGetPoint(long long contactId, glm::vec3* outPoint, glm::vec3* outNormal)
    {
        glm::vec3 point(0.0f), normal(0.0f);
        if (!Globals::physics.getContactPoint(contactId, point, normal))
            return 0;
        if (outPoint)  *outPoint = point;
        if (outNormal) *outNormal = normal;
        return 1;
    }

    int thunk_spatialQueryRadius(glm::vec3 position, float radius, Entity** outEntities, int maxOut)
    {
        if (!outEntities || maxOut <= 0)
            return 0;
        static std::vector<uint64> results; // main thread only, like every thunk
        Globals::spatialIndex.querySphere(glm::dvec3(position), radius, SpatialLayer_Render, results);
        const int count = glm::min(int(results.size()), maxOut);
        for (int i = 0; i < count; ++i)
            outEntities[i] = reinterpret_cast<Entity*>(results[i]);
        return count;
    }

    Entity* thunk_spatialGetNearestEntity(glm::vec3 position, float maxRadius, Entity* exclude)
    {
        return reinterpret_cast<Entity*>(Globals::spatialIndex.queryNearest(
            glm::dvec3(position), maxRadius, SpatialLayer_Render, reinterpret_cast<uint64>(exclude)));
    }

    void thunk_sendEvent(const char* eventName)
    {
        if (eventName)
            Globals::scriptEvents.fireEvent(eventName); // broadcast to every listener of this event
    }
    void thunk_sendEventToEntity(Entity* en, const char* eventName)
    {
        if (!en || !eventName)
            return;
        if (ScriptComponent* sc = getComponent<ScriptComponent>(en))
            sc->fireEvent(*en, eventName); // targeted: only this entity's script
    }

    // ---- force field ----
    void* thunk_entityGetForceComponent(Entity* en) { return en ? getComponent<ForceComponent>(en) : nullptr; }
    ForceComponent* asForce(void* p) { return static_cast<ForceComponent*>(p); }

    float thunk_forceGetOutput(void* p)       { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getOutput() : 0.0f; }
    float thunk_forceGetReach(void* p)        { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getReach() : 0.0f; }
    float thunk_forceGetFocus(void* p)        { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getFocus() : 0.5f; }
    float thunk_forceGetDistribution(void* p) { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getDistribution() : 0.5f; }
    float thunk_forceGetWidth(void* p)        { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getWidth() : 1.0f; }
    int   thunk_forceGetTeam(void* p)         { ForceComponent* fc = asForce(p); return fc ? int(fc->emitter.getTeam()) : 0; }
    glm::vec3 thunk_forceGetAppliedForce(void* p) { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getAppliedForce() : glm::vec3(0.0f); }
    float thunk_forceGetPressure(void* p)     { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getPressure() : 0.0f; }
    glm::vec3 thunk_forceGetLocalDirection(void* p) { ForceComponent* fc = asForce(p); return fc ? fc->localDirection : glm::vec3(0.0f, 0.0f, -1.0f); }
    glm::vec3 thunk_forceGetLocalOffset(void* p)    { ForceComponent* fc = asForce(p); return fc ? fc->localOffset : glm::vec3(0.0f); }
    int   thunk_forceGetCentered(void* p)           { ForceComponent* fc = asForce(p); return (fc && fc->centered) ? 1 : 0; }

    void thunk_forceSetOutput(void* p, float v)       { if (ForceComponent* fc = asForce(p)) fc->emitter.setOutput(v); }
    void thunk_forceSetReach(void* p, float v)        { if (ForceComponent* fc = asForce(p)) fc->emitter.setReach(v); }
    void thunk_forceSetFocus(void* p, float v)        { if (ForceComponent* fc = asForce(p)) fc->emitter.setFocus(v); }
    void thunk_forceSetDistribution(void* p, float v) { if (ForceComponent* fc = asForce(p)) fc->emitter.setDistribution(v); }
    void thunk_forceSetWidth(void* p, float v)        { if (ForceComponent* fc = asForce(p)) fc->emitter.setWidth(v); }
    void thunk_forceSetTeam(void* p, int v)           { if (ForceComponent* fc = asForce(p)) fc->emitter.setTeam(uint32(glm::max(v, 0))); }
    void thunk_forceSetLocalOffset(void* p, glm::vec3 v) { if (ForceComponent* fc = asForce(p)) fc->localOffset = v; }
    void thunk_forceSetCentered(void* p, int v)       { if (ForceComponent* fc = asForce(p)) fc->centered = (v != 0); }

    void thunk_forceSetLocalDirection(void* p, glm::vec3 v)
    {
        if (ForceComponent* fc = asForce(p))
        {
            // Match spawn: store a unit axis so the per-frame follow doesn't scale the field direction.
            const float len2 = glm::dot(v, v);
            fc->localDirection = len2 > 1e-12f ? v * glm::inversesqrt(len2) : glm::vec3(0.0f, 0.0f, -1.0f);
        }
    }

    // ---- scene component ----
    SceneComponent* asScene(void* p) { return static_cast<SceneComponent*>(p); }
    void* thunk_entityGetSceneComponent(Entity* en) { return en ? getComponent<SceneComponent>(en) : nullptr; }

    int thunk_sceneGetChildCount(void* p) { SceneComponent* sc = asScene(p); return sc ? (int)sc->children.size() : 0; }
    Entity* thunk_sceneFindChild(void* p, const char* name)
    {
        SceneComponent* sc = asScene(p);
        if (!sc || !name) return nullptr;
        for (const EntityPtr& child : sc->children)
            if (std::string_view(child->getName()) == name)
                return child.get();
        return nullptr;
    }
    Entity* thunk_sceneGetChildAt(void* p, int index)
    {
        SceneComponent* sc = asScene(p);
        if (sc && index >= 0 && index < (int)sc->children.size())
            return sc->children[index].get();
        return nullptr;
    }

    void thunk_sceneAddChild(void* p, Entity* child)
    {
        SceneComponent* sc = asScene(p);
        if (!sc || !child) return;
        Entity* parent = sc->getEntity();
        if (child->parent != parent) return;
        Globals::scriptEvents.addReparentRequest(EntityPtr(child), EntityPtr(parent));
    }

    void thunk_sceneRemoveChild(void* p, Entity* child)
    {
        SceneComponent* sc = asScene(p);
        if (!sc || !child) return;
        Entity* parent = sc->getEntity();
        if (child->parent != parent) return;
        Globals::scriptEvents.addReparentRequest(EntityPtr(child), EntityPtr(nullptr));
    }

    void thunk_sceneRemoveChildAt(void* p, int index)
    {
        thunk_sceneRemoveChild(p, thunk_sceneGetChildAt(p, index));
    }

    // ---- physics component ----
    void* thunk_entityGetPhysicsComponent(Entity* en) { return physicsOf(en); }

    int thunk_physicsIsAwake(void* p) 
    { 
        return static_cast<PhysicsComponent*>(p)->body.isAwake() ? 1 : 0;
    }

    glm::vec3 thunk_physicsGetVelocity(void* p) 
    { 
        return static_cast<PhysicsComponent*>(p)->body.getLinearVelocity(); 
    }

    void thunk_physicsSetVelocity(void* p, glm::vec3 v) 
    { 
        static_cast<PhysicsComponent*>(p)->body.setLinearVelocity(v); 
    }

    void thunk_physicsApplyImpulse(void* p, glm::vec3 v) 
    { 
        static_cast<PhysicsComponent*>(p)->body.applyImpulse(v); 
    }

    void thunk_physicsTeleport(void* p, glm::vec3 position, glm::vec3 eulerDeg)
    {
        PhysicsComponent* pc = static_cast<PhysicsComponent*>(p);
        if (pc->bodyType != EPhysicsBodyType::Dynamic)
            return; // kinematic/static bodies follow the entity; move those through the Entity mirror
        pc->body.setTransform(position, glm::quat(glm::radians(eulerDeg)));
    }

    // ---- audio component ----
    void* thunk_entityGetAudioComponent(Entity* en) { return en ? getComponent<AudioComponent>(en) : nullptr; }

    void thunk_audioTrigger(void* p, Entity* en, const char* alias, int overrideMask, glm::vec3 position, float volume, float pitch)
    {
        AudioComponent* ac = static_cast<AudioComponent*>(p);
        if (!ac || !en || !alias)
            return;
        AudioComponent::TriggerOverrides overrides;
        if (overrideMask & 1) overrides.position = position;
        if (overrideMask & 2) overrides.volume = volume;
        if (overrideMask & 4) overrides.pitch = pitch;
        ac->trigger(*en, alias, overrides);
    }

    void thunk_audioStop(void* p, const char* alias)
    {
        if (AudioComponent* ac = static_cast<AudioComponent*>(p))
            ac->stopSound(alias ? alias : "");
    }

    // ---- animator component ----
    // Everything routes through the state machine, which is the animator's gameplay interface: a script sets
    // parameters and the graph decides what plays. Reads of a never-set parameter return the type's zero (the
    // state machine's own contract), so there is no "does this parameter exist" question to answer.
    AnimStateMachine* animatorMachineOf(void* p)
    {
        AnimatorComponent* ac = static_cast<AnimatorComponent*>(p);
        return (ac != nullptr && ac->hasStateMachine) ? &ac->stateMachine : nullptr;
    }

    void* thunk_entityGetAnimatorComponent(Entity* en) { return en ? getComponent<AnimatorComponent>(en) : nullptr; }

    void thunk_animatorSetFloat(void* p, const char* name, float value)
    {
        if (AnimStateMachine* sm = animatorMachineOf(p); sm != nullptr && name != nullptr)
            sm->setFloat(name, value);
    }

    void thunk_animatorSetBool(void* p, const char* name, int value)
    {
        if (AnimStateMachine* sm = animatorMachineOf(p); sm != nullptr && name != nullptr)
            sm->setBool(name, value != 0);
    }

    void thunk_animatorSetTrigger(void* p, const char* name)
    {
        if (AnimStateMachine* sm = animatorMachineOf(p); sm != nullptr && name != nullptr)
            sm->setTrigger(name);
    }

    float thunk_animatorGetFloat(void* p, const char* name)
    {
        AnimStateMachine* sm = animatorMachineOf(p);
        return (sm != nullptr && name != nullptr) ? sm->getFloat(name) : 0.0f;
    }

    int thunk_animatorGetBool(void* p, const char* name)
    {
        AnimStateMachine* sm = animatorMachineOf(p);
        return (sm != nullptr && name != nullptr && sm->getBool(name)) ? 1 : 0;
    }

    const char* thunk_animatorGetStateName(void* p)
    {
        // Points into the state machine's own storage -- valid until the graph is rebuilt, the same lifetime
        // entityGetName's result has (see internString's comment in ScriptAPI.h: engine strings aren't interned).
        AnimStateMachine* sm = animatorMachineOf(p);
        return sm != nullptr ? sm->getCurrentStateName().c_str() : "";
    }

    float thunk_animatorGetTimeInState(void* p)
    {
        AnimStateMachine* sm = animatorMachineOf(p);
        return sm != nullptr ? sm->getTimeInState() : 0.0f;
    }

    float thunk_animatorGetSpeed(void* p)
    {
        const AnimatorComponent* ac = static_cast<const AnimatorComponent*>(p);
        return ac != nullptr ? ac->resolvePlaybackSpeed() : 0.0f;
    }

    int thunk_animatorGetEnabled(void* p)
    {
        const AnimatorComponent* ac = static_cast<const AnimatorComponent*>(p);
        return (ac != nullptr && ac->enabled) ? 1 : 0;
    }

    void thunk_animatorSetEnabled(void* p, int enabled)
    {
        if (AnimatorComponent* ac = static_cast<AnimatorComponent*>(p))
            ac->enabled = enabled != 0;
    }

    // ---- randomness ----
    // One generator for every script, living HERE rather than in a script DLL: a DLL-local one would reset its
    // state on each hot-reload, restarting the sequence mid-run. Not seeded from the clock -- a fixed seed means
    // a run is reproducible, which is worth more during development than unpredictability across launches.
    std::mt19937& scriptRng()
    {
        static std::mt19937 rng(0x5eed5eedu);
        return rng;
    }

    float thunk_randomFloat(float minValue, float maxValue)
    {
        if (!(minValue < maxValue))
            return minValue; // empty or inverted range -- one deterministic answer beats an assert in gameplay code
        return std::uniform_real_distribution<float>(minValue, maxValue)(scriptRng());
    }

    // ---- world ----
    VrRayHit thunk_worldRayCast(glm::vec3 origin, glm::vec3 direction, float maxDistance)
    {
        VrRayHit out{};
        const float len = glm::length(direction);
        if (len <= 0.0f || maxDistance <= 0.0f)
            return out; // a degenerate ray misses rather than asserting -- gameplay code shouldn't have to guard
        const PhysicsWorld::RayHit hit = Globals::physics.castRayClosest(origin, (direction / len) * maxDistance);
        if (!hit.hit)
            return out;
        out.hit = 1;
        out.point = hit.point;
        out.normal = hit.normal;
        out.distance = hit.fraction * maxDistance;
        return out;
    }

    glm::vec3 thunk_worldGetSunDirection(int) { return Globals::rendererVK.getSkyParams().sunDirection; }

    Entity* thunk_worldFindRootEntity(const char* displayName)
    {
        if (!displayName) return nullptr;
        // Linear over the root list: it is short (a scene's top level), and there is no name index to keep in
        // sync. Unnamed entities have a null name, which never matches.
        for (const EntityPtr& root : Globals::world.rootEntities())
            if (const char* name = root->getName(); name != nullptr && std::string_view(name) == displayName)
                return root.get();
        return nullptr;
    }

    void thunk_worldDrawLine(glm::vec3 from, glm::vec3 to, glm::vec3 color)
    {
        // The DSL speaks in linear 0..1 colours like everything else here; the renderer wants packed 0xAABBGGRR.
        const auto channel = [](float v) { return static_cast<uint32>(glm::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        Globals::rendererVK.addDebugLine(from, to,
            0xFF000000u | (channel(color.z) << 16) | (channel(color.y) << 8) | channel(color.x));
    }

    int thunk_randomInt(int minValue, int maxValue)
    {
        if (minValue > maxValue)
            return minValue;
        return std::uniform_int_distribution<int>(minValue, maxValue)(scriptRng()); // both ends inclusive
    }

}
#pragma warning(pop)

ScriptContext::ScriptContext()
    : log(&thunk_log)
    , logf(&thunk_logf)
#define SCRIPT_CTX_INIT(ret, name, ...) , name(&thunk_##name)
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_INIT)
#undef SCRIPT_CTX_INIT
{
    deltaSeconds = 0.0f;
    elapsedSeconds = 0.0f;
    cameraPosition = glm::vec3(0.0f);
    cameraDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    cameraFovDeg = 45.0f;
}

void ScriptContext::update(const Camera& camera, float newDeltaSeconds, float newElapsedSeconds)
{
    deltaSeconds = newDeltaSeconds;
    elapsedSeconds = newElapsedSeconds;

    // Forward/up are derived from the inverse view matrix (its -Z / Y columns).
    const glm::mat4 camToWorld = glm::inverse(camera.viewMatrix);
    cameraPosition = camera.position;
    cameraDirection = glm::normalize(-glm::vec3(camToWorld[2]));
    cameraUp = glm::normalize(glm::vec3(camToWorld[1]));
    cameraFovDeg = camera.fovDeg;
    cameraNear = camera.near;
    cameraFar = camera.far;
}

static_assert(offsetof(Entity, pos) == 0, "ScriptAPI.h Entity mirror out of sync: pos");
static_assert(offsetof(Entity, scale) == 12, "ScriptAPI.h Entity mirror out of sync: scale");
static_assert(offsetof(Entity, rot) == 16, "ScriptAPI.h Entity mirror out of sync: rot");
static_assert(offsetof(Entity, parent) == 32, "ScriptAPI.h Entity mirror out of sync: parent");