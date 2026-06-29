#include "ScriptAPI.h"
#include <cmath>

// Hand-authored test script for the visual-scripting hot-reload pipeline.
// Edit the values below and press F6 in the app to recompile + hot-reload live.

SCRIPT_EXPORT void ScriptInit(const ScriptContext* ctx)
{
    ctx->log("Test script initialized");
}

SCRIPT_EXPORT void ScriptUpdate(const ScriptContext* ctx, float dt)
{
    (void)dt;
    const float t = ctx->elapsedSeconds();

    // Orbit a warm point light around the origin.
    const float radius = 4.0f;
    const ScriptVec3 pos   = { cosf(t) * radius, 2.0f, sinf(t) * radius };
    const ScriptVec3 color = { 1.0f, 0.6f, 0.2f };
    ctx->spawnPointLight(pos, 25.0f, color, 60.0f);
}

SCRIPT_EXPORT void ScriptShutdown(const ScriptContext* ctx)
{
    ctx->log("Test script shut down");
}
