#pragma once

// ABI shared between the engine host and runtime-compiled visual-script DLLs.
//
// Plain C-style POD only: no glm, no STL, no engine types cross this boundary.
// That keeps script DLLs self-contained (they link no engine code, so the
// Globals:: singletons are never duplicated) and makes CRT mismatches harmless
// since only PODs, floats and raw pointers ever pass through.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScriptVec3 { float x, y, z; } ScriptVec3;

// Services the host exposes to scripts. The host fills these in; scripts only call them.
typedef struct ScriptContext
{
    void  (*log)(const char* message);
    float (*deltaSeconds)(void);
    float (*elapsedSeconds)(void);
    int   (*isKeyDown)(const char* keyName); // e.g. "Space", "A", "Left Shift"
    void  (*spawnPointLight)(ScriptVec3 position, float range, ScriptVec3 color, float intensity);
    void  (*setSun)(ScriptVec3 direction, ScriptVec3 color, float intensity);

    // The entity this script is attached to (set by the host before each ScriptUpdate). Null for the
    // global/panel test script. The entity* functions below all take this handle; they no-op / return
    // zero when it is null, so entity nodes are safe to use anywhere.
    void* self;

    // ---- entity reads ----
    ScriptVec3  (*entityGetPosition)(void* entity);     // local position
    float       (*entityGetScale)(void* entity);
    ScriptVec3  (*entityGetRotation)(void* entity);     // euler degrees
    ScriptVec3  (*entityGetForward)(void* entity);      // unit vectors in the entity's local frame
    ScriptVec3  (*entityGetRight)(void* entity);
    ScriptVec3  (*entityGetUp)(void* entity);
    const char* (*entityGetName)(void* entity);
    int         (*entityGetEnabled)(void* entity);
    int         (*entityGetChildCount)(void* entity);
    float       (*entityGetBoundsRadius)(void* entity); // world-space render bounds radius (0 if no mesh)

    // ---- entity writes ----
    void (*entitySetPosition)(void* entity, ScriptVec3 position);
    void (*entitySetScale)(void* entity, float scale);
    void (*entitySetRotation)(void* entity, ScriptVec3 eulerDegrees);
    void (*entitySetEnabled)(void* entity, int enabled);
    void (*entitySetAnimFloat)(void* entity, const char* param, float value);
    void (*entitySetAnimBool)(void* entity, const char* param, int value);
    void (*entitySetAnimTrigger)(void* entity, const char* param);
} ScriptContext;

// Entry points a script DLL exports. ScriptUpdate is required; Init/Shutdown are optional.
typedef void (*ScriptInitFn)(const ScriptContext*);
typedef void (*ScriptUpdateFn)(const ScriptContext*, float deltaSeconds);
typedef void (*ScriptShutdownFn)(const ScriptContext*);

#ifdef __cplusplus
}
#endif

// Convenience macro for script authors when declaring exported entry points.
#define SCRIPT_EXPORT extern "C" __declspec(dllexport)
