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

typedef struct Vec3 { float x, y, z; } Vec3;

// Services the host exposes to scripts. The host fills these in; scripts only call them.
typedef struct ScriptContext
{
    void  (*log)(const char* message);
    float (*deltaSeconds)(void);
    float (*elapsedSeconds)(void);
    int   (*isKeyDown)(const char* keyName); // e.g. "Space", "A", "Left Shift"
    void  (*spawnPointLight)(Vec3 position, float range, Vec3 color, float intensity);
    void  (*setSun)(Vec3 direction, Vec3 color, float intensity);

    // The entity* functions below take an entity handle, which ScriptUpdate receives as `self` and the
    // generated code passes through. They no-op / return zero for a null handle, so entity nodes are safe.

    // ---- entity reads ----
    Vec3        (*entityGetPosition)(void* entity);     // local position
    float       (*entityGetScale)(void* entity);
    Vec3        (*entityGetRotation)(void* entity);     // euler degrees
    Vec3        (*entityGetForward)(void* entity);      // unit vectors in the entity's local frame
    Vec3        (*entityGetRight)(void* entity);
    Vec3        (*entityGetUp)(void* entity);
    const char* (*entityGetName)(void* entity);
    int         (*entityGetEnabled)(void* entity);
    int         (*entityGetChildCount)(void* entity);
    float       (*entityGetBoundsRadius)(void* entity); // world-space render bounds radius (0 if no mesh)

    // ---- entity writes ----
    void (*entitySetPosition)(void* entity, Vec3 position);
    void (*entitySetScale)(void* entity, float scale);
    void (*entitySetRotation)(void* entity, Vec3 eulerDegrees);
    void (*entitySetEnabled)(void* entity, int enabled);
    void (*entitySetAnimFloat)(void* entity, const char* param, float value);
    void (*entitySetAnimBool)(void* entity, const char* param, int value);
    void (*entitySetAnimTrigger)(void* entity, const char* param);

#ifdef __cplusplus
    ScriptContext(); // the engine binds all the function pointers here; scripts never construct one
#endif
} ScriptContext;

// Entry points a script DLL exports. ScriptUpdate is required; Init/Shutdown are optional. `self` is the
// entity the script is running on (null for none); entity nodes pass it to the entity* functions.
typedef void (*ScriptInitFn)(const ScriptContext*);
typedef void (*ScriptUpdateFn)(const ScriptContext*, void* self, float deltaSeconds);
typedef void (*ScriptShutdownFn)(const ScriptContext*);

#ifdef __cplusplus
}
#endif

// Convenience macro for script authors when declaring exported entry points.
#define SCRIPT_EXPORT extern "C" __declspec(dllexport)
