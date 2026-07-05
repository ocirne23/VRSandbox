#pragma once

// ABI shared between the engine host and runtime-compiled visual-script DLLs.
//
// Only glm vectors, PODs, floats and raw pointers cross this boundary — no STL, no engine types. That keeps
// script DLLs self-contained (they link no engine code, so the Globals:: singletons are never duplicated)
// and makes CRT mismatches harmless. Vec3 is glm::vec3 (header-only, links nothing) so graph math nodes can
// use glm operators/functions directly; both sides agree on its 12-byte layout, so the ABI is stable.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

#ifdef __cplusplus
extern "C" {
#endif

// Entity handle. In scripts (compiled with /DSCRIPT_BUILD) this is a layout-compatible MIRROR of the engine
// Entity (Code/Entity/Private/EntityDef.ixx), so script code can read/write self->pos / self->scale /
// self->rot / self->parent directly instead of only through the ctx->entity* calls.
//
// Only the members BEFORE the engine Entity's `std::string displayName` are mirrored: std::string has a
// different layout under the engine's debug CRT than the script's /MD, which would misalign everything past
// it. Reach the rest (name, enabled, children, ...) through the ctx->entity* functions. KEEP THESE FIELDS IN
// SYNC with EntityDef.ixx — the engine static_asserts the offsets (ScriptContext.cpp) so drift fails the build.
//
// Host-side this stays a forward declaration: the real engine Entity is the actual type; defining the mirror
// here would clash with it.
#ifdef SCRIPT_BUILD
struct Entity
{
    glm::vec3 pos;    // local position
    float     scale;
    glm::quat rot;
    Entity*   parent; // null for a root entity
};
#else
class Entity;
#endif

// Services the host exposes to scripts. The host fills these in; scripts only call them.
typedef struct ScriptContext
{
    // Per-frame data, refreshed once a frame by update() (see below) before any script runs — plain fields
    // instead of function pointers so reading them is free of an indirect call.
    float deltaSeconds;
    float elapsedSeconds;

    // The active render camera, snapshotted for the frame.
    glm::vec3 cameraPosition;
    glm::vec3 cameraDirection; // forward, normalized
    glm::vec3 cameraUp;        // normalized
    float     cameraFovDeg;

    void  (*log)(const char* message);
    void  (*logf)(const char* message, ...);
    int   (*isKeyDown)(const char* keyName); // e.g. "Space", "A", "Left Shift"
    void  (*spawnPointLight)(glm::vec3 position, float range, glm::vec3 color, float intensity);
    void  (*setSun)(glm::vec3 direction, glm::vec3 color, float intensity);
    Entity* (*spawnEntity)(const char* assetPath, glm::vec3 position); // spawns an asset/prefab at world position now
                                                                        // and returns it (added to the scene root
                                                                        // once this frame's scripts finish running)
    void  (*destroyEntity)(Entity* entity);                          // queues the entity (e.g. self) for removal

    // The entity* functions below take an entity handle, which ScriptUpdate receives as `self` and the
    // generated code passes through. They no-op / return zero for a null handle, so entity nodes are safe.
    //
    // Position/scale/rotation are NOT here: the Entity mirror above already exposes them as plain fields
    // (self->pos / self->scale / self->rot), so generated code reads/writes them directly and uses glm::
    // helpers for anything derived (glm::degrees/radians for euler angles, self->rot * axis for a basis
    // vector, glm::eulerAngles for the reverse) instead of going through the ABI.

    // ---- entity reads ----
    const char* (*entityGetName)(Entity* entity);
    int         (*entityGetEnabled)(Entity* entity);
    int         (*entityGetChildCount)(Entity* entity);
    float       (*entityGetBoundsRadius)(Entity* entity); // world-space render bounds radius (0 if no mesh)
    Entity*     (*entityFindChild)(Entity* entity, const char* name); // direct child by name, null if none match
    Entity*     (*entityGetChildAt)(Entity* entity, int index);       // direct child by index, null if out of range

    // ---- entity writes ----
    void (*entitySetEnabled)(Entity* entity, int enabled);
    void (*entitySetAnimFloat)(Entity* entity, const char* param, float value);
    void (*entitySetAnimBool)(Entity* entity, const char* param, int value);
    void (*entitySetAnimTrigger)(Entity* entity, const char* param);
    void (*entityAddChild)(Entity* parent, Entity* child);    // reparents child under parent (no-op: cycle, or parent has no scene component)
    void (*entityRemoveChild)(Entity* parent, Entity* child); // detaches child from parent, becoming a root entity (no-op if child isn't parent's)
    void (*entityRemoveChildAt)(Entity* parent, int index);   // same as entityRemoveChild, by child index

    // ---- physics (appended after the entity block — keep this table append-only so cached script DLLs
    // compiled against an older layout keep working). The entity* functions target the entity's
    // PhysicsComponent body and no-op / return zero when there is none.
    void      (*physicsSetGravity)(glm::vec3 gravity);
    int       (*physicsRayCast)(glm::vec3 origin, glm::vec3 translation, // ray = origin + translation, 1 on hit
                                glm::vec3* outPoint, glm::vec3* outNormal, float* outFraction);
    int       (*entityHasPhysics)(Entity* entity);
    int       (*entityIsPhysicsAwake)(Entity* entity);
    glm::vec3 (*entityGetVelocity)(Entity* entity);
    void      (*entitySetVelocity)(Entity* entity, glm::vec3 velocity);
    void      (*entityApplyImpulse)(Entity* entity, glm::vec3 impulse); // world space, at the center of mass, wakes the body
    void      (*entityTeleportPhysics)(Entity* entity, glm::vec3 position, glm::vec3 eulerDeg); // dynamic bodies only;
                                // kinematic/static bodies follow the entity, so move those via the Entity mirror instead

    // ---- events ---- fire an On Event entry by NAME (the host maps it to each script's local index). sendEvent
    // reaches every script listening for that event; sendEventToEntity reaches only the given entity's script.
    void      (*sendEvent)(const char* eventName);
    void      (*sendEventToEntity)(Entity* entity, const char* eventName);

    // ---- audio (appended; keep this table append-only) ---- target the entity's AudioComponent; entities
    // without one (or without the alias) no-op. overrideMask picks which override arguments replace the
    // sound's authored settings: 1 = position (also pins the sound there instead of following the entity),
    // 2 = volume, 4 = pitch. entityStopAudio with a null/empty alias stops every sound on the entity.
    void      (*entityTriggerAudio)(Entity* entity, const char* alias, int overrideMask, glm::vec3 position, float volume, float pitch);
    void      (*entityStopAudio)(Entity* entity, const char* alias);

#ifdef __cplusplus
    ScriptContext(); // the engine binds all the function pointers here; scripts never construct one

    // Refreshes the per-frame fields above. Called once per frame by the host (main.cpp), before any script
    // runs; scripts never call this themselves. Defined in ScriptContext.cpp (needs Core.Time), so this
    // header stays dependency-free.
    void update(float newDeltaSeconds, float newElapsedSeconds,
        glm::vec3 newCameraPosition, glm::vec3 newCameraDirection, glm::vec3 newCameraUp, float newCameraFovDeg);
#endif
} ScriptContext;

// Entry points a script DLL exports. ScriptUpdate is required; Init/Shutdown are optional. `self` is the
// entity the script is running on (null for none); entity nodes pass it to the entity* functions.
// `scriptData` points at the entity's persistent per-instance memory block (or null when the script declares
// no data). Scripts that use a Script Data node cast it to their generated `ScriptData` struct; the host
// sizes and zero-inits the block from the script's ScriptDataSize() export (see below).
typedef void (*ScriptOnSpawnFn)(const ScriptContext*, Entity* self, void* scriptData);
typedef void (*ScriptOnDestroyFn)(const ScriptContext*, Entity* self, void* scriptData);
typedef void (*ScriptUpdateFn)(const ScriptContext*, Entity* self, float deltaSeconds, void* scriptData);

// Optional export: fires an On Event entry by index (e.g. an animation notify). eventIdx is the entry's
// position among the script's On Event nodes' entries, in declaration order — the host (not the script) is
// the one that knows event NAMES; it resolves a name to an index via ScriptEventCount/ScriptEventName below
// and caches it, so no string is passed (or compared) at fire time.
typedef void (*ScriptOnEventFn)(const ScriptContext*, Entity* self, int eventIdx, void* scriptData);

// Optional exports: the script's On Event entry names, in the same order as the eventIdx OnEvent expects.
// Absent (or zero count) means the script declares no On Event entries.
typedef int         (*ScriptEventCountFn)(void);
typedef const char* (*ScriptEventNameFn)(int eventIdx);

// Optional export: the byte size of the script's persistent ScriptData struct. When present, the host
// allocates a zeroed block of this size per entity and passes it to ScriptUpdate as `scriptData`. Absent (or
// zero) means the script keeps no persistent memory. Using sizeof(ScriptData) here lets the compiler settle
// struct padding/alignment, so the host never has to compute layout itself.
typedef unsigned int (*ScriptDataSizeFn)(void);

#ifdef __cplusplus
}
#endif

// Convenience macro for script authors when declaring exported entry points.
#define SCRIPT_EXPORT extern "C" __declspec(dllexport)
