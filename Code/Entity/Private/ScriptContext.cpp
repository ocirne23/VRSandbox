module;

#include <cstdarg>
#include <cstddef>
#include "ScriptAPI.h"

module Entity;

import Core;
import Core.glm;
import Core.Log;
import Core.Time;
import Core.SDL;
import Core.Sphere;
import RendererVK;
import Animation;
import :Entity;
import :Component;

// Implements the script ABI (ScriptAPI.h) against the engine and drives per-entity script execution.
// The Script library is a pure compiler/loader; this file owns the ScriptContext and all the thunks, so
// the renderer / SDL / animation dependencies live here in Entity (which already imports them) rather
// than leaking into Script.

// Scripts use a layout mirror of Entity (ScriptAPI.h, /DSCRIPT_BUILD) to read self->pos etc. directly. These
// assert the real engine layout matches that mirror — if Entity's leading members move, update both.
static_assert(offsetof(Entity, pos)    == 0,  "ScriptAPI.h Entity mirror out of sync: pos");
static_assert(offsetof(Entity, scale)  == 12, "ScriptAPI.h Entity mirror out of sync: scale");
static_assert(offsetof(Entity, rot)    == 16, "ScriptAPI.h Entity mirror out of sync: rot");
static_assert(offsetof(Entity, parent) == 32, "ScriptAPI.h Entity mirror out of sync: parent");

namespace
{
    // ---- context thunks -----------------------------------------------------

    void thunk_log(const char* message) { Log::info(message ? message : ""); }
    void thunk_logf(const char* message, ...) 
    {
        va_list args;
        va_start(args, message);
        int size_s = std::snprintf(nullptr, 0, message, args) + 1;
		if (size_s <= 0) { va_end(args); return; }
		size_t size = static_cast<size_t>(size_s);
		std::string formattedString(size, '\0');
		std::vsnprintf(&formattedString[0], size, message, args);
        Log::info(formattedString);
        va_end(args);
    }
    int thunk_isKeyDown(const char* keyName)
    {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        return (keys && keyName) ? (keys[scancodeFromName(keyName)] ? 1 : 0) : 0;
    }

    void thunk_spawnPointLight(glm::vec3 position, float range, glm::vec3 color, float intensity)
    {
        Globals::rendererVK.addPointLight(PointLight(position, range, color, intensity));
    }

    void thunk_setSun(glm::vec3 direction, glm::vec3 color, float intensity)
    {
        Globals::rendererVK.setSunLight(direction, color, intensity);
    }

    void thunk_spawnEntity(const char* assetPath, glm::vec3 position)
    {
        if (assetPath)
            Globals::scriptSpawnRequests.push_back({ assetPath, position });
    }

    void thunk_destroyEntity(Entity* e)
    {
        Globals::scriptDestroyRequests.push_back(e);
    }

    const char* thunk_entityGetName(Entity* e) { return e->displayName.c_str(); }

    int thunk_entityGetEnabled(Entity* en)
    {
        if (SceneComponent* sc = getComponent<SceneComponent>(en))
            return sc->enabled ? 1 : 0;
		return 1;
    }
    int thunk_entityGetChildCount(Entity* en)
    {
        if (SceneComponent* sc = getComponent<SceneComponent>(en))
			return (int)sc->children.size();
        return 0;
    }
    float thunk_entityGetBoundsRadius(Entity* en)
    {
		if (RenderComponent* rc = getComponent<RenderComponent>(en))
			return rc->node.getWorldBounds().radius;
        return 0.0f;
    }
    Entity* thunk_entityFindChild(Entity* en, const char* name)
    {
        if (!en || !name) return nullptr;
        if (SceneComponent* sc = getComponent<SceneComponent>(en))
            for (const EntityPtr& child : sc->children)
                if (child->displayName == name)
                    return child.get();
        return nullptr;
    }

    void thunk_entitySetEnabled(Entity* en, int enabled)              { if (SceneComponent* sc = getComponent<SceneComponent>(en)) sc->enabled = enabled != 0; }
    void thunk_entitySetAnimFloat(Entity* en, const char* p, float v) { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setFloat(p, v); }
    void thunk_entitySetAnimBool(Entity * en, const char* p, int v)   { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setBool(p, v != 0); }
    void thunk_entitySetAnimTrigger(Entity* en, const char* p)        { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setTrigger(p); }

}

// Binds the ABI function-pointer table to the engine thunks. Globals::scriptContext is constructed at
// startup, so the context is ready before any script runs. (Scripts never construct a ScriptContext, so
// their DLLs never need this definition.)
ScriptContext::ScriptContext()
{
    deltaSeconds = 0.0f;
    elapsedSeconds = 0.0f;
    cameraPosition = glm::vec3(0.0f);
    cameraDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    cameraFovDeg = 45.0f;

    log = &thunk_log;
    logf = &thunk_logf;
    isKeyDown = &thunk_isKeyDown;
    spawnPointLight = &thunk_spawnPointLight;
    setSun = &thunk_setSun;
    spawnEntity = &thunk_spawnEntity;
    destroyEntity = &thunk_destroyEntity;
    entityGetName = &thunk_entityGetName;
    entityGetEnabled = &thunk_entityGetEnabled;
    entityGetChildCount = &thunk_entityGetChildCount;
    entityGetBoundsRadius = &thunk_entityGetBoundsRadius;
    entityFindChild = &thunk_entityFindChild;
    entitySetEnabled = &thunk_entitySetEnabled;
    entitySetAnimFloat = &thunk_entitySetAnimFloat;
    entitySetAnimBool = &thunk_entitySetAnimBool;
    entitySetAnimTrigger = &thunk_entitySetAnimTrigger;
}

// Refreshes the per-frame fields; called once a frame (main.cpp) before any script runs this frame.
void ScriptContext::update(float newDeltaSeconds, float newElapsedSeconds,
    glm::vec3 newCameraPosition, glm::vec3 newCameraDirection, glm::vec3 newCameraUp, float newCameraFovDeg)
{
    deltaSeconds = newDeltaSeconds;
    elapsedSeconds = newElapsedSeconds;
    cameraPosition = newCameraPosition;
    cameraDirection = newCameraDirection;
    cameraUp = newCameraUp;
    cameraFovDeg = newCameraFovDeg;
}

