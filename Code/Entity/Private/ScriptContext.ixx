module;

#include "ScriptAPI.h"

export module Entity:ScriptContext;

export namespace Globals
{
    ScriptContext scriptContext;
}

// Registers the DSL's base vocabulary (vec2/vec3/vec4/quat, self/physics/audio/force/Engine, the 5 ScriptAPI
// entry points) against Globals::scriptBindings -- Script's own registry has no built-in content of its own
// (see ScriptBindings.ixx), so THIS call is what guarantees it exists. Must run once from main, before anything
// touches the registry (ScriptEditor's ScriptBindings::build() first, in practice) -- same explicit-call
// convention as ScriptEventManager::initialize().
export void registerScriptDslBindings();
