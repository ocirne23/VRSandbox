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
    void  (*log)(const char* message);
    void  (*logf)(const char* message, ...);
    float (*deltaSeconds)(void);
    float (*elapsedSeconds)(void);
    int   (*isKeyDown)(const char* keyName); // e.g. "Space", "A", "Left Shift"
    void  (*spawnPointLight)(glm::vec3 position, float range, glm::vec3 color, float intensity);
    void  (*setSun)(glm::vec3 direction, glm::vec3 color, float intensity);
    void  (*spawnEntity)(const char* assetPath, glm::vec3 position); // queues an asset/prefab spawn at world position
    void  (*destroyEntity)(Entity* entity);                          // queues the entity (e.g. self) for removal

    // The entity* functions below take an entity handle, which ScriptUpdate receives as `self` and the
    // generated code passes through. They no-op / return zero for a null handle, so entity nodes are safe.

    // ---- entity reads ----
    glm::vec3   (*entityGetPosition)(Entity* entity);     // local position
    float       (*entityGetScale)(Entity* entity);
    glm::vec3   (*entityGetRotation)(Entity* entity);     // euler degrees
    glm::vec3   (*entityGetForward)(Entity* entity);      // unit vectors in the entity's local frame
    glm::vec3   (*entityGetRight)(Entity* entity);
    glm::vec3   (*entityGetUp)(Entity* entity);
    const char* (*entityGetName)(Entity* entity);
    int         (*entityGetEnabled)(Entity* entity);
    int         (*entityGetChildCount)(Entity* entity);
    float       (*entityGetBoundsRadius)(Entity* entity); // world-space render bounds radius (0 if no mesh)

    // ---- entity writes ----
    void (*entitySetPosition)(Entity* entity, glm::vec3 position);
    void (*entitySetScale)(Entity* entity, float scale);
    void (*entitySetRotation)(Entity* entity, glm::vec3 eulerDegrees);
    void (*entitySetEnabled)(Entity* entity, int enabled);
    void (*entitySetAnimFloat)(Entity* entity, const char* param, float value);
    void (*entitySetAnimBool)(Entity* entity, const char* param, int value);
    void (*entitySetAnimTrigger)(Entity* entity, const char* param);

#ifdef __cplusplus
    ScriptContext(); // the engine binds all the function pointers here; scripts never construct one
#endif
} ScriptContext;

// Entry points a script DLL exports. ScriptUpdate is required; Init/Shutdown are optional. `self` is the
// entity the script is running on (null for none); entity nodes pass it to the entity* functions.
typedef void (*ScriptInitFn)(const ScriptContext*);
typedef void (*ScriptUpdateFn)(const ScriptContext*, Entity* self, float deltaSeconds);
typedef void (*ScriptShutdownFn)(const ScriptContext*);

#ifdef __cplusplus
}
#endif

// Convenience macro for script authors when declaring exported entry points.
#define SCRIPT_EXPORT extern "C" __declspec(dllexport)
