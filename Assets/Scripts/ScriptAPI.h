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
