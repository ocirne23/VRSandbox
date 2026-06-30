export module Entity:ScriptCommands;

import Core;
import Core.glm;

// Deferred entity create/destroy requested by scripts, drained by App after the entity update pass
// (scripts run mid entity-tree-walk, so the entity list can't be mutated inline).
//
// The destroy queue stores opaque void* rather than Entity*: it is written from ScriptContext.cpp (which
// includes the ABI header's global forward-declared Entity) and read from App (the module-attached Entity).
// Those two Entity types don't merge under MSVC modules, so a shared Globals symbol whose mangled name
// embeds Entity would fail to link. void* keeps the symbol name Entity-free; pointer conversion is implicit.
export struct ScriptSpawnRequest
{
    std::string assetPath;
    glm::vec3   position;
};

export namespace Globals
{
    std::vector<ScriptSpawnRequest> scriptSpawnRequests;
    std::vector<void*>              scriptDestroyRequests;
}
