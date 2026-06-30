module;

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
    float thunk_deltaSeconds(void) { return (float)Globals::time.getDeltaSec(); }
    float thunk_elapsedSeconds(void) { return (float)Globals::time.getElapsedSec(); }

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

    glm::vec3 thunk_entityGetPosition(Entity* e) { return e->pos; }
    float thunk_entityGetScale(Entity* e) { return e->scale; }
    glm::vec3 thunk_entityGetRotation(Entity* e) { return glm::degrees(glm::eulerAngles(e->rot)); }
    glm::vec3 thunk_entityGetForward(Entity* e) { return e->rot * glm::vec3(0, 0, -1); }
    glm::vec3 thunk_entityGetRight(Entity* e) { return e->rot * glm::vec3(1, 0, 0); }
    glm::vec3 thunk_entityGetUp(Entity* e) { return e->rot * glm::vec3(0, 1, 0); }
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

    void thunk_entitySetPosition(Entity* en, glm::vec3 v) { en->pos = v; }
    void thunk_entitySetScale(Entity* en, float s)   { en->scale = s; }
    void thunk_entitySetRotation(Entity* en, glm::vec3 d) { en->rot = glm::quat(glm::radians(d)); }
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
    log = &thunk_log;
    deltaSeconds = &thunk_deltaSeconds;
    elapsedSeconds = &thunk_elapsedSeconds;
    isKeyDown = &thunk_isKeyDown;
    spawnPointLight = &thunk_spawnPointLight;
    setSun = &thunk_setSun;
    spawnEntity = &thunk_spawnEntity;
    destroyEntity = &thunk_destroyEntity;
    entityGetPosition = &thunk_entityGetPosition;
    entityGetScale = &thunk_entityGetScale;
    entityGetRotation = &thunk_entityGetRotation;
    entityGetForward = &thunk_entityGetForward;
    entityGetRight = &thunk_entityGetRight;
    entityGetUp = &thunk_entityGetUp;
    entityGetName = &thunk_entityGetName;
    entityGetEnabled = &thunk_entityGetEnabled;
    entityGetChildCount = &thunk_entityGetChildCount;
    entityGetBoundsRadius = &thunk_entityGetBoundsRadius;
    entitySetPosition = &thunk_entitySetPosition;
    entitySetScale = &thunk_entitySetScale;
    entitySetRotation = &thunk_entitySetRotation;
    entitySetEnabled = &thunk_entitySetEnabled;
    entitySetAnimFloat = &thunk_entitySetAnimFloat;
    entitySetAnimBool = &thunk_entitySetAnimBool;
    entitySetAnimTrigger = &thunk_entitySetAnimTrigger;
}

