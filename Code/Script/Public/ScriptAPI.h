#pragma once

// ABI shared between the engine host and runtime-compiled visual-script DLLs.
//
// Only glm vectors, PODs, floats and raw pointers cross this boundary â€” no STL, no engine types. That keeps
// script DLLs self-contained (they link no engine code, so the Globals:: singletons are never duplicated)
// and makes CRT mismatches harmless. Vec3 is glm::vec3 (header-only, links nothing) so graph math nodes can
// use glm operators/functions directly; both sides agree on its 12-byte layout, so the ABI is stable.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstdarg>

#ifdef __cplusplus
extern "C" {
#endif

// Entity handle. In scripts (compiled with /DSCRIPT_BUILD) this is a layout-compatible MIRROR of the engine
// Entity (Code/Entity/Private/EntityDef.ixx), so script code can read/write self->pos / self->scale /
// self->rot / self->parent directly instead of only through the ctx->entity* calls.
//
// Only the members BEFORE the engine Entity's `displayName` are mirrored: past it the layout depends on
// engine-internal types the script side cannot see. Reach the rest (name, enabled, children, ...) through the
// ctx->entity* functions (ctx->entityGetName for the display name). KEEP THESE FIELDS IN
// SYNC with EntityDef.ixx â€” the engine static_asserts the offsets (ScriptContext.cpp) so drift fails the build.
//
// Host-side this stays a forward declaration: the real engine Entity is the actual type; defining the mirror
// here would clash with it.
#if defined(SCRIPT_BUILD) || defined(SCRIPT_STATIC_BUILD)
// Both script back-ends (hot-reload DLL and cooked static) use the layout mirror rather than the engine type:
// `Entity*` only ever crosses to the ctx->* thunks as an opaque pointer, and mirror Entity* and the real
// Entity* share the same representation, so the thunks work the same in one binary as across the DLL boundary.
// (Importing the real Entity module here instead would drag glm in twice — module + textual — and clash.)
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
struct Camera; // engine render camera; only the host-only update() below touches it (scripts never see it)

#pragma warning(push)
#pragma warning(disable: 4190) // has C-linkage specified, but returns 'glm::vec<3,float,glm::packed_highp>' which is incompatible with..

// The ctx-> function set (all but log/logf), listed once as (return type, name, params, call args) with the docs.
// The host expands it into the const function-pointer TABLE; the cooked build (SCRIPT_STATIC_BUILD) into inline
// forwarders + the engine-side thunk_* decls, so ctx->foo(x) inlines under LTCG. log/logf are declared by hand
// alongside each expansion: logf is variadic (can't forward uniformly) and log rides with it so the pointer
// table keeps its ABI order (log, logf, then this list). A signature drifting from the real thunk fails to build.
#define SCRIPT_CTX_FUNCS(X) \
    X(int,         isKeyDown,              (const char* keyName), (keyName)) /* e.g. "Space", "A", "Left Shift" */ \
    X(void,        spawnPointLight,        (glm::vec3 position, float range, glm::vec3 color, float intensity), (position, range, color, intensity)) \
    X(void,        setSun,                 (glm::vec3 direction, glm::vec3 color, float intensity), (direction, color, intensity)) \
    X(Entity*,     spawnEntity,            (const char* assetPath, glm::vec3 position), (assetPath, position)) \
    X(void,        destroyEntity,          (Entity* entity), (entity)) /* queues the entity (e.g. self) for removal */ \
    /* ---- entity reads ---- */ \
    X(const char*, entityGetName,          (Entity* entity), (entity)) \
    X(int,         entityGetEnabled,       (Entity* entity), (entity)) \
    X(int,         entityGetChildCount,    (Entity* entity), (entity)) /* used by the Get Entity node's Children output; child hierarchy nodes go through the SceneComponent handle (below) */ \
    X(float,       entityGetBoundsRadius,  (Entity* entity), (entity)) /* world-space render bounds radius (0 if no mesh) */ \
    /* ---- entity writes ---- */ \
    X(void,        entitySetEnabled,       (Entity* entity, int enabled), (entity, enabled)) \
    X(void,        entitySetAnimFloat,     (Entity* entity, const char* param, float value), (entity, param, value)) \
    X(void,        entitySetAnimBool,      (Entity* entity, const char* param, int value), (entity, param, value)) \
    X(void,        entitySetAnimTrigger,   (Entity* entity, const char* param), (entity, param)) \
    /* ---- physics (global) ---- */ \
    X(void,        physicsSetGravity,      (glm::vec3 gravity), (gravity)) \
    X(int,         physicsRayCast,         (glm::vec3 origin, glm::vec3 translation, glm::vec3* outPoint, glm::vec3* outNormal, float* outFraction), (origin, translation, outPoint, outNormal, outFraction)) /* ray = origin + translation, 1 on hit */ \
    X(float,       physicsRayCastDistance, (glm::vec3 origin, glm::vec3 dir, float maxDist), (origin, dir, maxDist)) /* dir need not be pre-normalized; distance to the closest hit within maxDist, or maxDist itself on a miss (never a sentinel outside the query range) */ \
    /* ---- events ---- */ \
    X(void,        sendEvent,              (const char* eventName), (eventName)) \
    X(void,        sendEventToEntity,      (Entity* entity, const char* eventName), (entity, eventName)) \
    /* ---- physics contact queries ---- */ \
    X(int,         physicsContactGetPoint, (long long contactId, glm::vec3* outPoint, glm::vec3* outNormal), (contactId, outPoint, outNormal)) \
    /* ---- spatial queries ---- */ \
    X(int,         spatialQueryRadius,     (glm::vec3 position, float radius, Entity** outEntities, int maxOut), (position, radius, outEntities, maxOut)) \
    X(Entity*,     spatialGetNearestEntity,(glm::vec3 position, float maxRadius, Entity* exclude), (position, maxRadius, exclude)) \
    /* ---- force field ----  */ \
    X(void*,       entityGetForceComponent,(Entity* entity), (entity)) \
    X(float,       forceGetOutput,         (void* forceComponent), (forceComponent)) \
    X(float,       forceGetReach,          (void* forceComponent), (forceComponent)) \
    X(float,       forceGetFocus,          (void* forceComponent), (forceComponent)) \
    X(float,       forceGetDistribution,   (void* forceComponent), (forceComponent)) \
    X(float,       forceGetWidth,          (void* forceComponent), (forceComponent)) \
    X(int,         forceGetTeam,           (void* forceComponent), (forceComponent)) \
    X(glm::vec3,   forceGetAppliedForce,   (void* forceComponent), (forceComponent)) \
    X(float,       forceGetPressure,       (void* forceComponent), (forceComponent)) \
    X(glm::vec3,   forceGetLocalDirection, (void* forceComponent), (forceComponent)) \
    X(glm::vec3,   forceGetLocalOffset,    (void* forceComponent), (forceComponent)) \
    X(int,         forceGetCentered,       (void* forceComponent), (forceComponent)) \
    X(void,        forceSetOutput,         (void* forceComponent, float output), (forceComponent, output)) \
    X(void,        forceSetReach,          (void* forceComponent, float reach), (forceComponent, reach)) \
    X(void,        forceSetFocus,          (void* forceComponent, float focus), (forceComponent, focus)) \
    X(void,        forceSetDistribution,   (void* forceComponent, float distribution), (forceComponent, distribution)) \
    X(void,        forceSetWidth,          (void* forceComponent, float width), (forceComponent, width)) \
    X(void,        forceSetTeam,           (void* forceComponent, int team), (forceComponent, team)) \
    X(void,        forceSetLocalDirection, (void* forceComponent, glm::vec3 direction), (forceComponent, direction)) \
    X(void,        forceSetLocalOffset,    (void* forceComponent, glm::vec3 offset), (forceComponent, offset)) \
    X(void,        forceSetCentered,       (void* forceComponent, int centered), (forceComponent, centered)) \
    /* ---- scene component ---- */ \
    X(void*,       entityGetSceneComponent,(Entity* entity), (entity)) \
    X(int,         sceneGetChildCount,     (void* sceneComponent), (sceneComponent)) \
    X(Entity*,     sceneFindChild,         (void* sceneComponent, const char* name), (sceneComponent, name)) /* direct child by name, null if none match */ \
    X(Entity*,     sceneGetChildAt,        (void* sceneComponent, int index), (sceneComponent, index)) /* direct child by index, null if out of range */ \
    X(void,        sceneAddChild,          (void* parentSceneComponent, Entity* child), (parentSceneComponent, child)) /* reparents child under the component's owner (no-op on a cycle / wrong current parent) */ \
    X(void,        sceneRemoveChild,       (void* parentSceneComponent, Entity* child), (parentSceneComponent, child)) /* detaches child, becoming a root entity (no-op if not the component owner's child) */ \
    X(void,        sceneRemoveChildAt,     (void* parentSceneComponent, int index), (parentSceneComponent, index)) /* same as sceneRemoveChild, by child index */ \
    /* ---- physics component ---- */ \
    X(void*,       entityGetPhysicsComponent,(Entity* entity), (entity)) \
    X(int,         physicsIsAwake,         (void* physicsComponent), (physicsComponent)) \
    X(glm::vec3,   physicsGetVelocity,     (void* physicsComponent), (physicsComponent)) \
    X(void,        physicsSetVelocity,     (void* physicsComponent, glm::vec3 velocity), (physicsComponent, velocity)) \
    X(void,        physicsApplyImpulse,    (void* physicsComponent, glm::vec3 impulse), (physicsComponent, impulse)) /* world space, at the center of mass, wakes the body */ \
    X(void,        physicsTeleport,        (void* physicsComponent, glm::vec3 position, glm::vec3 eulerDeg), (physicsComponent, position, eulerDeg)) /* dynamic bodies only */ \
    /* ---- audio component ---- */ \
    X(void*,       entityGetAudioComponent,(Entity* entity), (entity)) \
    X(void,        audioTrigger,           (void* audioComponent, Entity* entity, const char* alias, int overrideMask, glm::vec3 position, float volume, float pitch), (audioComponent, entity, alias, overrideMask, position, volume, pitch)) \
    X(void,        audioStop,              (void* audioComponent, const char* alias), (audioComponent, alias))

#if defined(SCRIPT_STATIC_BUILD)
// Cooked build: the engine thunks the inline ctx methods forward to (defined extern "C" in ScriptContext.cpp,
// which links into the same binary, so LTCG inlines them). thunk_vlogf is logf's va_list-based backing.
extern "C" {
    void thunk_log(const char* message);       // log/logf are outside SCRIPT_CTX_FUNCS (see there); declared by hand
    void thunk_vlogf(const char* fmt, va_list ap);
#define SCRIPT_CTX_DECL(ret, name, params, args) ret thunk_##name params;
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_DECL)
#undef SCRIPT_CTX_DECL
}
#endif

// Services the host exposes to scripts. The host fills these in; scripts only call them.
typedef struct ScriptContext
{
    // Per-frame data, refreshed once a frame by update() (see below) before any script runs â€” plain fields
    // instead of function pointers so reading them is free of an indirect call.
    float deltaSeconds;
    float elapsedSeconds;

    // The active render camera, snapshotted for the frame.
    glm::vec3 cameraPosition;
    glm::vec3 cameraDirection; // forward, normalized
    glm::vec3 cameraUp;        // normalized
    float     cameraFovDeg;
    float     cameraNear;
	float     cameraFar;

#if defined(SCRIPT_STATIC_BUILD)
    // Cooked build: `ctx->foo(x)` is an inline forwarder to `thunk_foo(x)` (the engine thunk), so LTCG inlines it
    // instead of an indirect call. `this` is unused — the data fields above are laid out identically to the
    // host's pointer-table version, so `ctx->deltaSeconds` etc. still hit the right offset.
    void log(const char* message) const { thunk_log(message); }
    void logf(const char* fmt, ...) const { va_list ap; va_start(ap, fmt); thunk_vlogf(fmt, ap); va_end(ap); }
#define SCRIPT_CTX_METHOD(ret, name, params, args) ret name params const { return thunk_##name args; }
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_METHOD)
#undef SCRIPT_CTX_METHOD
#else
    void  (* const log)(const char* message);
    void  (* const logf)(const char* message, ...);
    // The rest of the const function-pointer table, generated from SCRIPT_CTX_FUNCS (docs live there). Members
    // stay in list order after log/logf, which is the ABI layout cached script DLLs bind against — append only.
#define SCRIPT_CTX_PTR(ret, name, params, args) ret (* const name) params;
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_PTR)
#undef SCRIPT_CTX_PTR

#ifdef __cplusplus
#ifndef SCRIPT_BUILD
    ScriptContext(); // the engine binds all the function pointers here; scripts never construct one

    // Refreshes the per-frame fields above from the frame's active render camera. Called once per frame by the
    // host (main.cpp), before any script runs; scripts never call this themselves (it's compiled out of script
    // DLLs, so the engine Camera type never leaks across the ABI). Defined in ScriptContext.cpp.
    void update(const Camera& camera, float newDeltaSeconds, float newElapsedSeconds);
#endif // SCRIPT_BUILD
#endif // __cplusplus
#endif // SCRIPT_STATIC_BUILD (inline forwarders) vs. the pointer table
} ScriptContext;

#pragma warning(pop)

// Entry points a script DLL exports. ScriptUpdate is required; Init/Shutdown are optional. `self` is the
// entity the script is running on (null for none); entity nodes pass it to the entity* functions.
// `scriptData` points at the entity's persistent per-instance memory block (or null when the script declares
// no data). Scripts that use a Script Data node cast it to their generated `ScriptData` struct; the host
// sizes and zero-inits the block from the script's ScriptDataSize() export (see below).
typedef void (*ScriptOnSpawnFn)(const ScriptContext*, Entity* self, void* scriptData);
typedef void (*ScriptOnDestroyFn)(const ScriptContext*, Entity* self, void* scriptData);
typedef void (*ScriptUpdateFn)(const ScriptContext*, Entity* self, float deltaSeconds, void* scriptData);

// Optional export: fires an On Event entry by index (e.g. an animation notify). eventIdx is the entry's
// position among the script's On Event nodes' entries, in declaration order â€” the host (not the script) is
// the one that knows event NAMES; it resolves a name to an index via ScriptEventCount/ScriptEventName below
// and caches it, so no string is passed (or compared) at fire time.
typedef void (*ScriptOnEventFn)(const ScriptContext*, Entity* self, int eventIdx, void* scriptData);

// Optional exports: the script's On Event entry names, in the same order as the eventIdx OnEvent expects.
// Absent (or zero count) means the script declares no On Event entries.
typedef int         (*ScriptEventCountFn)(void);
typedef const char* (*ScriptEventNameFn)(int eventIdx);

// Optional export: fires when this entity's PhysicsComponent takes part in a contact begin/end or sensor
// begin/end overlap (see dispatchPhysicsContactEvents). A single fixed entry point (like OnSpawn/OnDestroy),
// not indexed like OnEvent â€” a script gets one On Physics Event node, not user-named entries. `other` is the
// entity on the other side of the contact (null if it has none, e.g. a raw static collider); `contactId`
// identifies the underlying contact for THIS frame only (see ctx->physicsContactGetPoint).
typedef void (*ScriptOnPhysicsEventFn)(const ScriptContext*, Entity* self, Entity* other, int begin, int sensor,
    long long contactId, void* scriptData);

// Optional export: the byte size of the script's persistent ScriptData struct. When present, the host
// allocates a zeroed block of this size per entity and passes it to ScriptUpdate as `scriptData`. Absent (or
// zero) means the script keeps no persistent memory. Using sizeof(ScriptData) here lets the compiler settle
// struct padding/alignment, so the host never has to compute layout itself.
typedef unsigned int (*ScriptDataSizeFn)(void);

#ifdef __cplusplus
}
#endif

// ---- static/cooked build registry ----
// When SCRIPTS_STATIC is on, scripts are compiled INTO the engine (the App-Scripts target #includes each .scr into
// its own namespace) instead of to runtime DLLs. The kinds below index one entry-point function each; a script
// registers the ones it actually defines through the REGISTER_*() macros below, and ScriptHost resolves a path to
// that set (no cl, no LoadLibrary) so the engine calls whole-program-optimized, inline-able script code. Visible
// to the script TUs (SCRIPT_STATIC_BUILD) and to the engine host (SCRIPTS_STATIC).
#if defined(SCRIPT_STATIC_BUILD) || defined(SCRIPTS_STATIC)
enum VrScriptEntryKind
{
    VR_SCRIPT_ON_SPAWN = 0,
    VR_SCRIPT_UPDATE,
    VR_SCRIPT_ON_DESTROY,
    VR_SCRIPT_ON_EVENT,
    VR_SCRIPT_ON_PHYSICS_EVENT,
    VR_SCRIPT_DATA_SIZE,
    VR_SCRIPT_EVENT_COUNT,
    VR_SCRIPT_EVENT_NAME,
    VR_SCRIPT_ENTRY_COUNT
};
#ifdef __cplusplus
extern "C" {
#endif
// Defined in the Script library (ScriptHost.cpp). Records one entry-point function for a script path; called by
// the REGISTER_*() macros at static-init from inside each script's namespace in the App-Scripts aggregate.
void vrRegisterScriptEntry(const char* scriptPath, int kind, void* fn);
#ifdef __cplusplus
}
#endif
#endif

// Entry-point linkage. DLL build: C exports resolved by name (GetProcAddress) — ScriptHost force-includes this
// header before the .scr. Static/cooked build (SCRIPT_STATIC_BUILD): nothing — the entry points stay plain free
// functions, and the App-Scripts aggregate #includes each .scr into its own namespace, so `Script_Foo::Update`
// etc. can't collide across scripts.
#if defined(SCRIPT_STATIC_BUILD)
#define SCRIPT_EXPORT
#else
#define SCRIPT_EXPORT extern "C" __declspec(dllexport)
#endif

// Each .scr emits a REGISTER_*() after the entry points it defines. In the DLL build they vanish (the entry points
// are C exports GetProcAddress finds by name). In the static build each expands to a namespace-scope static whose
// initializer records that script's function pointer — so the aggregate needs no per-script parsing, the file
// says what it has. VR_CURRENT_SCRIPT is #defined to the script's path by the aggregate around each #include.
#if defined(SCRIPT_STATIC_BUILD)
#define REGISTER_ON_SPAWN()          static const int _vrRegOnSpawn   = (vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_ON_SPAWN,         (void*)&OnSpawn), 0);
#define REGISTER_UPDATE()            static const int _vrRegUpdate    = (vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_UPDATE,          (void*)&Update), 0);
#define REGISTER_ON_DESTROY()        static const int _vrRegOnDestroy = (vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_ON_DESTROY,      (void*)&OnDestroy), 0);
#define REGISTER_ON_PHYSICS_EVENT()  static const int _vrRegOnPhys    = (vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_ON_PHYSICS_EVENT,(void*)&OnPhysicsEvent), 0);
#define REGISTER_SCRIPT_DATA_SIZE()  static const int _vrRegDataSize  = (vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_DATA_SIZE,       (void*)&ScriptDataSize), 0);
#define REGISTER_ON_EVENT()          static const int _vrRegOnEvent   = (vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_ON_EVENT,        (void*)&OnEvent), \
                                                                        vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_EVENT_COUNT,     (void*)&ScriptEventCount), \
                                                                        vrRegisterScriptEntry(VR_CURRENT_SCRIPT, VR_SCRIPT_EVENT_NAME,      (void*)&ScriptEventName), 0);
#else
#define REGISTER_ON_SPAWN()
#define REGISTER_UPDATE()
#define REGISTER_ON_DESTROY()
#define REGISTER_ON_PHYSICS_EVENT()
#define REGISTER_SCRIPT_DATA_SIZE()
#define REGISTER_ON_EVENT()
#endif
