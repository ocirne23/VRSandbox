module;

#include "ScriptAPI.h"

export module Entity:ScriptContext;

// The script ABI context: the function-pointer table the engine fills and passes to every ScriptUpdate.
// It binds itself in its constructor (defined in ScriptContext.cpp, which owns the engine thunks), so this
// global is ready before any script runs. Exposed as Globals::scriptContext alongside the other singletons.
export namespace Globals
{
    ScriptContext scriptContext;
}
