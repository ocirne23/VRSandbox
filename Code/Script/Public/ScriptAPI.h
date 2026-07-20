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

// The full ctx-> function set (all but the variadic logf), listed once: (return type, name, params, call args).
// The host still uses the const function-pointer table below; the cooked build (SCRIPT_STATIC_BUILD) instead
// expands this into inline forwarders + the engine-side thunk_* decls, so ctx->foo(x) inlines under LTCG.
#define SCRIPT_CTX_FUNCS(X) \
    X(void,        log,                    (const char* a0), (a0)) \
    X(int,         isKeyDown,              (const char* a0), (a0)) \
    X(void,        spawnPointLight,        (glm::vec3 a0, float a1, glm::vec3 a2, float a3), (a0, a1, a2, a3)) \
    X(void,        setSun,                 (glm::vec3 a0, glm::vec3 a1, float a2), (a0, a1, a2)) \
    X(Entity*,     spawnEntity,            (const char* a0, glm::vec3 a1), (a0, a1)) \
    X(void,        destroyEntity,          (Entity* a0), (a0)) \
    X(const char*, entityGetName,          (Entity* a0), (a0)) \
    X(int,         entityGetEnabled,       (Entity* a0), (a0)) \
    X(int,         entityGetChildCount,    (Entity* a0), (a0)) \
    X(float,       entityGetBoundsRadius,  (Entity* a0), (a0)) \
    X(Entity*,     entityFindChild,        (Entity* a0, const char* a1), (a0, a1)) \
    X(Entity*,     entityGetChildAt,       (Entity* a0, int a1), (a0, a1)) \
    X(void,        entitySetEnabled,       (Entity* a0, int a1), (a0, a1)) \
    X(void,        entitySetAnimFloat,     (Entity* a0, const char* a1, float a2), (a0, a1, a2)) \
    X(void,        entitySetAnimBool,      (Entity* a0, const char* a1, int a2), (a0, a1, a2)) \
    X(void,        entitySetAnimTrigger,   (Entity* a0, const char* a1), (a0, a1)) \
    X(void,        entityAddChild,         (Entity* a0, Entity* a1), (a0, a1)) \
    X(void,        entityRemoveChild,      (Entity* a0, Entity* a1), (a0, a1)) \
    X(void,        entityRemoveChildAt,    (Entity* a0, int a1), (a0, a1)) \
    X(void,        physicsSetGravity,      (glm::vec3 a0), (a0)) \
    X(int,         physicsRayCast,         (glm::vec3 a0, glm::vec3 a1, glm::vec3* a2, glm::vec3* a3, float* a4), (a0, a1, a2, a3, a4)) \
    X(int,         entityHasPhysics,       (Entity* a0), (a0)) \
    X(int,         entityIsPhysicsAwake,   (Entity* a0), (a0)) \
    X(glm::vec3,   entityGetVelocity,      (Entity* a0), (a0)) \
    X(void,        entitySetVelocity,      (Entity* a0, glm::vec3 a1), (a0, a1)) \
    X(void,        entityApplyImpulse,     (Entity* a0, glm::vec3 a1), (a0, a1)) \
    X(void,        entityTeleportPhysics,  (Entity* a0, glm::vec3 a1, glm::vec3 a2), (a0, a1, a2)) \
    X(void,        sendEvent,              (const char* a0), (a0)) \
    X(void,        sendEventToEntity,      (Entity* a0, const char* a1), (a0, a1)) \
    X(void,        entityTriggerAudio,     (Entity* a0, const char* a1, int a2, glm::vec3 a3, float a4, float a5), (a0, a1, a2, a3, a4, a5)) \
    X(void,        entityStopAudio,        (Entity* a0, const char* a1), (a0, a1)) \
    X(int,         physicsContactGetPoint, (long long a0, glm::vec3* a1, glm::vec3* a2), (a0, a1, a2)) \
    X(int,         spatialQueryRadius,     (glm::vec3 a0, float a1, Entity** a2, int a3), (a0, a1, a2, a3)) \
    X(Entity*,     spatialGetNearestEntity,(glm::vec3 a0, float a1, Entity* a2), (a0, a1, a2)) \
    X(void*,       entityGetForceComponent,(Entity* a0), (a0)) \
    X(float,       forceGetOutput,         (void* a0), (a0)) \
    X(float,       forceGetReach,          (void* a0), (a0)) \
    X(float,       forceGetFocus,          (void* a0), (a0)) \
    X(float,       forceGetDistribution,   (void* a0), (a0)) \
    X(float,       forceGetWidth,          (void* a0), (a0)) \
    X(int,         forceGetTeam,           (void* a0), (a0)) \
    X(glm::vec3,   forceGetAppliedForce,   (void* a0), (a0)) \
    X(float,       forceGetPressure,       (void* a0), (a0)) \
    X(glm::vec3,   forceGetLocalDirection, (void* a0), (a0)) \
    X(glm::vec3,   forceGetLocalOffset,    (void* a0), (a0)) \
    X(int,         forceGetCentered,       (void* a0), (a0)) \
    X(void,        forceSetOutput,         (void* a0, float a1), (a0, a1)) \
    X(void,        forceSetReach,          (void* a0, float a1), (a0, a1)) \
    X(void,        forceSetFocus,          (void* a0, float a1), (a0, a1)) \
    X(void,        forceSetDistribution,   (void* a0, float a1), (a0, a1)) \
    X(void,        forceSetWidth,          (void* a0, float a1), (a0, a1)) \
    X(void,        forceSetTeam,           (void* a0, int a1), (a0, a1)) \
    X(void,        forceSetLocalDirection, (void* a0, glm::vec3 a1), (a0, a1)) \
    X(void,        forceSetLocalOffset,    (void* a0, glm::vec3 a1), (a0, a1)) \
    X(void,        forceSetCentered,       (void* a0, int a1), (a0, a1))

#if defined(SCRIPT_STATIC_BUILD)
// Cooked build: the engine thunks the inline ctx methods forward to (defined extern "C" in ScriptContext.cpp,
// which links into the same binary, so LTCG inlines them). thunk_vlogf is logf's va_list-based backing.
extern "C" {
#define SCRIPT_CTX_DECL(ret, name, params, args) ret thunk_##name params;
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_DECL)
#undef SCRIPT_CTX_DECL
    void thunk_vlogf(const char* fmt, va_list ap);
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
#define SCRIPT_CTX_METHOD(ret, name, params, args) ret name params const { return thunk_##name args; }
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_METHOD)
#undef SCRIPT_CTX_METHOD
    void logf(const char* fmt, ...) const { va_list ap; va_start(ap, fmt); thunk_vlogf(fmt, ap); va_end(ap); }
#else
    void  (* const log)(const char* message);
    void  (* const logf)(const char* message, ...);
    int   (* const isKeyDown)(const char* keyName); // e.g. "Space", "A", "Left Shift"
    void  (* const spawnPointLight)(glm::vec3 position, float range, glm::vec3 color, float intensity);
    void  (* const setSun)(glm::vec3 direction, glm::vec3 color, float intensity);
    Entity* (* const spawnEntity)(const char* assetPath, glm::vec3 position); // spawns an asset/prefab at world position now
                                                                        // and returns it (added to the scene root
                                                                        // once this frame's scripts finish running)
    void  (* const destroyEntity)(Entity* entity);                          // queues the entity (e.g. self) for removal

    // The entity* functions below take an entity handle, which ScriptUpdate receives as `self` and the
    // generated code passes through. They no-op / return zero for a null handle, so entity nodes are safe.
    //
    // Position/scale/rotation are NOT here: the Entity mirror above already exposes them as plain fields
    // (self->pos / self->scale / self->rot), so generated code reads/writes them directly and uses glm::
    // helpers for anything derived (glm::degrees/radians for euler angles, self->rot * axis for a basis
    // vector, glm::eulerAngles for the reverse) instead of going through the ABI.

    // ---- entity reads ----
    const char* (* const entityGetName)(Entity* entity);
    int         (* const entityGetEnabled)(Entity* entity);
    int         (* const entityGetChildCount)(Entity* entity);
    float       (* const entityGetBoundsRadius)(Entity* entity); // world-space render bounds radius (0 if no mesh)
    Entity*     (* const entityFindChild)(Entity* entity, const char* name); // direct child by name, null if none match
    Entity*     (* const entityGetChildAt)(Entity* entity, int index);       // direct child by index, null if out of range

    // ---- entity writes ----
    void (* const entitySetEnabled)(Entity* entity, int enabled);
    void (* const entitySetAnimFloat)(Entity* entity, const char* param, float value);
    void (* const entitySetAnimBool)(Entity* entity, const char* param, int value);
    void (* const entitySetAnimTrigger)(Entity* entity, const char* param);
    void (* const entityAddChild)(Entity* parent, Entity* child);    // reparents child under parent (no-op: cycle, or parent has no scene component)
    void (* const entityRemoveChild)(Entity* parent, Entity* child); // detaches child from parent, becoming a root entity (no-op if child isn't parent's)
    void (* const entityRemoveChildAt)(Entity* parent, int index);   // same as entityRemoveChild, by child index

    // ---- physics (appended after the entity block â€” keep this table append-only so cached script DLLs
    // compiled against an older layout keep working). The entity* functions target the entity's
    // PhysicsComponent body and no-op / return zero when there is none.
    void      (* const physicsSetGravity)(glm::vec3 gravity);
    int       (* const physicsRayCast)(glm::vec3 origin, glm::vec3 translation, // ray = origin + translation, 1 on hit
                                glm::vec3* outPoint, glm::vec3* outNormal, float* outFraction);
    int       (* const entityHasPhysics)(Entity* entity);
    int       (* const entityIsPhysicsAwake)(Entity* entity);
    glm::vec3 (* const entityGetVelocity)(Entity* entity);
    void      (* const entitySetVelocity)(Entity* entity, glm::vec3 velocity);
    void      (* const entityApplyImpulse)(Entity* entity, glm::vec3 impulse); // world space, at the center of mass, wakes the body
    void      (* const entityTeleportPhysics)(Entity* entity, glm::vec3 position, glm::vec3 eulerDeg); // dynamic bodies only;
                                // kinematic/static bodies follow the entity, so move those via the Entity mirror instead

    // ---- events ---- fire an On Event entry by NAME (the host maps it to each script's local index). sendEvent
    // reaches every script listening for that event; sendEventToEntity reaches only the given entity's script.
    void      (* const sendEvent)(const char* eventName);
    void      (* const sendEventToEntity)(Entity* entity, const char* eventName);

    // ---- audio (appended; keep this table append-only) ---- target the entity's AudioComponent; entities
    // without one (or without the alias) no-op. overrideMask picks which override arguments replace the
    // sound's authored settings: 1 = position (also pins the sound there instead of following the entity),
    // 2 = volume, 4 = pitch. entityStopAudio with a null/empty alias stops every sound on the entity.
    void      (* const entityTriggerAudio)(Entity* entity, const char* alias, int overrideMask, glm::vec3 position, float volume, float pitch);
    void      (* const entityStopAudio)(Entity* entity, const char* alias);

    // ---- physics contact queries (appended; keep this table append-only) ---- resolves a contactId from an
    // On Physics Event node to its first manifold point in world space. Only valid for the frame the event
    // fired (until the next physics step recycles the contact); returns 0 (outPoint/outNormal untouched) for
    // a stale id or a sensor event (sensors have no contact manifold).
    int       (* const physicsContactGetPoint)(long long contactId, glm::vec3* outPoint, glm::vec3* outNormal);

    // ---- spatial queries (appended; keep this table append-only) ---- backed by the SpatialIndex,
    // so they stay fast with huge entity counts. spatialQueryRadius fills outEntities with every entity
    // whose render bounds intersect the sphere (up to maxOut) and returns the count written;
    // spatialGetNearestEntity returns the entity with the nearest origin within maxRadius (null if none),
    // skipping `exclude` (pass self).
    int     (* const spatialQueryRadius)(glm::vec3 position, float radius, Entity** outEntities, int maxOut);
    Entity* (* const spatialGetNearestEntity)(glm::vec3 position, float maxRadius, Entity* exclude);

    // ---- force field (appended; keep this table append-only) ---- entityGetForceComponent resolves the
    // entity's ForceComponent ONCE and hands back an opaque handle (null if it has none); every force* call
    // takes that handle and casts it back, so a chain of get/sets never repeats the component lookup. The
    // handle stays valid until the entity (or its force component) is destroyed — fetch it fresh each frame,
    // don't cache it across frames. force* getters return a type default for a null handle and setters no-op.
    // Applied force / pressure are the ~2-frame-old GPU readbacks; team clamps a negative set to 0; local
    // direction is re-normalized on set; local offset is the span start (reach stays world units).
    void*     (* const entityGetForceComponent)(Entity* entity);
    float     (* const forceGetOutput)(void* forceComponent);
    float     (* const forceGetReach)(void* forceComponent);
    float     (* const forceGetFocus)(void* forceComponent);
    float     (* const forceGetDistribution)(void* forceComponent);
    float     (* const forceGetWidth)(void* forceComponent);
    int       (* const forceGetTeam)(void* forceComponent);
    glm::vec3 (* const forceGetAppliedForce)(void* forceComponent);
    float     (* const forceGetPressure)(void* forceComponent);
    glm::vec3 (* const forceGetLocalDirection)(void* forceComponent);
    glm::vec3 (* const forceGetLocalOffset)(void* forceComponent);
    int       (* const forceGetCentered)(void* forceComponent);
    void      (* const forceSetOutput)(void* forceComponent, float output);
    void      (* const forceSetReach)(void* forceComponent, float reach);
    void      (* const forceSetFocus)(void* forceComponent, float focus);
    void      (* const forceSetDistribution)(void* forceComponent, float distribution);
    void      (* const forceSetWidth)(void* forceComponent, float width);
    void      (* const forceSetTeam)(void* forceComponent, int team);
    void      (* const forceSetLocalDirection)(void* forceComponent, glm::vec3 direction);
    void      (* const forceSetLocalOffset)(void* forceComponent, glm::vec3 offset);
    void      (* const forceSetCentered)(void* forceComponent, int centered);

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
