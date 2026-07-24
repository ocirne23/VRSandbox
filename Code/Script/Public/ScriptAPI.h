#pragma once

// ABI shared between the engine host and runtime-compiled visual-script DLLs.
// Only ABI compatible types; glm vectors, PODs, floats and raw pointers cross this boundary no STL, no engine types. That keeps

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstdarg>

#include "ScriptCtxMacros.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(SCRIPT_BUILD) || defined(SCRIPT_STATIC_BUILD)
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

#define SCRIPT_CTX_FUNCS(X) \
    X(int,         isKeyDown,              (const char*, keyName)) /* e.g. "Space", "A", "Left Shift" */ \
    X(void,        spawnPointLight,        (glm::vec3, position), (float, range), (glm::vec3, color), (float, intensity)) \
    X(void,        setSun,                 (glm::vec3, direction), (glm::vec3, color), (float, intensity)) \
    X(Entity*,     spawnEntity,            (const char*, assetPath), (glm::vec3, position)) \
    X(void,        destroyEntity,          (Entity*, entity)) /* queues the entity (e.g. self) for removal */ \
    /* ---- entity reads ---- */ \
    X(const char*, entityGetName,          (Entity*, entity)) \
    X(int,         entityGetEnabled,       (Entity*, entity)) \
    X(int,         entityGetChildCount,    (Entity*, entity)) /* used by the Get Entity node's Children output; child hierarchy nodes go through the SceneComponent handle (below) */ \
    X(float,       entityGetBoundsRadius,  (Entity*, entity)) /* world-space render bounds radius (0 if no mesh) */ \
    /* ---- entity writes ---- */ \
    X(void,        entitySetEnabled,       (Entity*, entity), (int, enabled)) \
    X(void,        entitySetAnimFloat,     (Entity*, entity), (const char*, param), (float, value)) \
    X(void,        entitySetAnimBool,      (Entity*, entity), (const char*, param), (int, value)) \
    X(void,        entitySetAnimTrigger,   (Entity*, entity), (const char*, param)) \
    /* ---- physics (global) ---- */ \
    X(void,        physicsSetGravity,      (glm::vec3, gravity)) \
    X(int,         physicsRayCast,         (glm::vec3, origin), (glm::vec3, translation), (glm::vec3*, outPoint), (glm::vec3*, outNormal), (float*, outFraction)) /* ray = origin + translation, 1 on hit */ \
    X(float,       physicsRayCastDistance, (glm::vec3, origin), (glm::vec3, dir), (float, maxDist)) /* dir need not be pre-normalized; distance to the closest hit within maxDist, or maxDist itself on a miss (never a sentinel outside the query range) */ \
    /* ---- events ---- */ \
    X(void,        sendEvent,              (const char*, eventName)) \
    X(void,        sendEventToEntity,      (Entity*, entity), (const char*, eventName)) \
    /* ---- physics contact queries ---- */ \
    X(int,         physicsContactGetPoint, (long long, contactId), (glm::vec3*, outPoint), (glm::vec3*, outNormal)) \
    /* ---- spatial queries ---- */ \
    X(int,         spatialQueryRadius,     (glm::vec3, position), (float, radius), (Entity**, outEntities), (int, maxOut)) \
    X(Entity*,     spatialGetNearestEntity,(glm::vec3, position), (float, maxRadius), (Entity*, exclude)) \
    /* ---- force field ----  */ \
    X(void*,       entityGetForceComponent,(Entity*, entity)) \
    X(float,       forceGetOutput,         (void*, forceComponent)) \
    X(float,       forceGetReach,          (void*, forceComponent)) \
    X(float,       forceGetFocus,          (void*, forceComponent)) \
    X(float,       forceGetDistribution,   (void*, forceComponent)) \
    X(float,       forceGetWidth,          (void*, forceComponent)) \
    X(int,         forceGetTeam,           (void*, forceComponent)) \
    X(glm::vec3,   forceGetAppliedForce,   (void*, forceComponent)) \
    X(float,       forceGetPressure,       (void*, forceComponent)) \
    X(glm::vec3,   forceGetLocalDirection, (void*, forceComponent)) \
    X(glm::vec3,   forceGetLocalOffset,    (void*, forceComponent)) \
    X(int,         forceGetCentered,       (void*, forceComponent)) \
    X(void,        forceSetOutput,         (void*, forceComponent), (float, output)) \
    X(void,        forceSetReach,          (void*, forceComponent), (float, reach)) \
    X(void,        forceSetFocus,          (void*, forceComponent), (float, focus)) \
    X(void,        forceSetDistribution,   (void*, forceComponent), (float, distribution)) \
    X(void,        forceSetWidth,          (void*, forceComponent), (float, width)) \
    X(void,        forceSetTeam,           (void*, forceComponent), (int, team)) \
    X(void,        forceSetLocalDirection, (void*, forceComponent), (glm::vec3, direction)) \
    X(void,        forceSetLocalOffset,    (void*, forceComponent), (glm::vec3, offset)) \
    X(void,        forceSetCentered,       (void*, forceComponent), (int, centered)) \
    /* ---- scene component ---- */ \
    X(void*,       entityGetSceneComponent,(Entity*, entity)) \
    X(int,         sceneGetChildCount,     (void*, sceneComponent)) \
    X(Entity*,     sceneFindChild,         (void*, sceneComponent), (const char*, name)) /* direct child by name, null if none match */ \
    X(Entity*,     sceneGetChildAt,        (void*, sceneComponent), (int, index)) /* direct child by index, null if out of range */ \
    X(void,        sceneAddChild,          (void*, parentSceneComponent), (Entity*, child)) /* reparents child under the component's owner (no-op on a cycle / wrong current parent) */ \
    X(void,        sceneRemoveChild,       (void*, parentSceneComponent), (Entity*, child)) /* detaches child, becoming a root entity (no-op if not the component owner's child) */ \
    X(void,        sceneRemoveChildAt,     (void*, parentSceneComponent), (int, index)) /* same as sceneRemoveChild, by child index */ \
    /* ---- physics component ---- */ \
    X(void*,       entityGetPhysicsComponent,(Entity*, entity)) \
    X(int,         physicsIsAwake,         (void*, physicsComponent)) \
    X(glm::vec3,   physicsGetVelocity,     (void*, physicsComponent)) \
    X(void,        physicsSetVelocity,     (void*, physicsComponent), (glm::vec3, velocity)) \
    X(void,        physicsApplyImpulse,    (void*, physicsComponent), (glm::vec3, impulse)) /* world space, at the center of mass, wakes the body */ \
    X(void,        physicsTeleport,        (void*, physicsComponent), (glm::vec3, position), (glm::vec3, eulerDeg)) /* dynamic bodies only */ \
    /* ---- audio component ---- */ \
    X(void*,       entityGetAudioComponent,(Entity*, entity)) \
    X(void,        audioTrigger,           (void*, audioComponent), (Entity*, entity), (const char*, alias), (int, overrideMask), (glm::vec3, position), (float, volume), (float, pitch)) \
    X(void,        audioStop,              (void*, audioComponent), (const char*, alias))

#if defined(SCRIPT_STATIC_BUILD)
// Cooked build: the engine thunks the inline ctx methods forward to (defined extern "C" in ScriptContext.cpp,
extern "C" {
    void thunk_log(const char* message);       // log/logf are outside SCRIPT_CTX_FUNCS (see there); declared by hand
    void thunk_vlogf(const char* fmt, va_list ap);
#define SCRIPT_CTX_DECL(ret, name, ...) ret thunk_##name (SCRIPT_CTX_PARAMS(__VA_ARGS__));
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_DECL)
#undef SCRIPT_CTX_DECL
}
#endif

// Services the host exposes to scripts. The host fills these in; scripts only call them.
typedef struct ScriptContext
{
    // Per-frame data
    float deltaSeconds;
    float elapsedSeconds;
    glm::vec3 cameraPosition;
    glm::vec3 cameraDirection;
    glm::vec3 cameraUp;
    float     cameraFovDeg;
    float     cameraNear;
	float     cameraFar;

#if defined(SCRIPT_STATIC_BUILD)
    // Cooked build: `ctx->foo(x)` is an inline forwarder to `thunk_foo(x)` (the engine thunk), so LTCG inlines it
    void log(const char* message) const { thunk_log(message); }
    void logf(const char* fmt, ...) const { va_list ap; va_start(ap, fmt); thunk_vlogf(fmt, ap); va_end(ap); }
#define SCRIPT_CTX_METHOD(ret, name, ...) ret name (SCRIPT_CTX_PARAMS(__VA_ARGS__)) const { return thunk_##name (SCRIPT_CTX_ARGS(__VA_ARGS__)); }
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_METHOD)
#undef SCRIPT_CTX_METHOD
#else
    void  (* const log)(const char* message);
    void  (* const logf)(const char* message, ...);
    // The rest of the const function-pointer table, generated from SCRIPT_CTX_FUNCS (docs live there).
#define SCRIPT_CTX_PTR(ret, name, ...) ret (* const name) (SCRIPT_CTX_PARAMS(__VA_ARGS__));
    SCRIPT_CTX_FUNCS(SCRIPT_CTX_PTR)
#undef SCRIPT_CTX_PTR

#ifdef __cplusplus
#ifndef SCRIPT_BUILD
    ScriptContext(); // the engine binds all the function pointers here; scripts never construct one

    void update(const Camera& camera, float newDeltaSeconds, float newElapsedSeconds);
#endif // SCRIPT_BUILD
#endif // __cplusplus
#endif // SCRIPT_STATIC_BUILD (inline forwarders) vs. the pointer table
} ScriptContext;

#pragma warning(pop)

typedef void (*ScriptOnSpawnFn)(const ScriptContext*, Entity* self, void* scriptData);
typedef void (*ScriptOnDestroyFn)(const ScriptContext*, Entity* self, void* scriptData);
typedef void (*ScriptUpdateFn)(const ScriptContext*, Entity* self, float deltaSeconds, void* scriptData);
typedef void (*ScriptOnEventFn)(const ScriptContext*, Entity* self, int eventIdx, void* scriptData);
typedef int          (*ScriptEventCountFn)(void);
typedef const char*  (*ScriptEventNameFn)(int eventIdx);
typedef void         (*ScriptOnPhysicsEventFn)(const ScriptContext*, Entity* self, Entity* other, int begin, int sensor, long long contactId, void* scriptData);
typedef unsigned int (*ScriptDataSizeFn)(void);

#ifdef __cplusplus
}
#endif

// ---- static/cooked build registry ----
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
void vrRegisterScriptEntry(const char* scriptPath, int kind, void* fn);
#ifdef __cplusplus
}
#endif
#endif

// Entry-point linkage. DLL build
#if defined(SCRIPT_STATIC_BUILD)
#define SCRIPT_EXPORT
#else
#define SCRIPT_EXPORT extern "C" __declspec(dllexport)
#endif

// Each .scr/.dsl emits a REGISTER_*() after the entry points it defines.
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
