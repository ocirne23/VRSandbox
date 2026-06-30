module;

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

    void thunk_spawnPointLight(Vec3 position, float range, Vec3 color, float intensity)
    {
        Globals::rendererVK.addPointLight(PointLight(glm::vec3(position.x, position.y, position.z), range,
            glm::vec3(color.x, color.y, color.z), intensity));
    }

    void thunk_setSun(Vec3 direction, Vec3 color, float intensity)
    {
        Globals::rendererVK.setSunLight(glm::vec3(direction.x, direction.y, direction.z),
            glm::vec3(color.x, color.y, color.z), intensity);
    }

    Entity* asEntity(void* e) { return static_cast<Entity*>(e); }
    Vec3 toScript(const glm::vec3& v) { return Vec3{ v.x, v.y, v.z }; }

    Vec3 thunk_entityGetPosition(void* e) { Entity* en = asEntity(e); return en ? toScript(en->pos) : Vec3{ 0, 0, 0 }; }
    float thunk_entityGetScale(void* e) { Entity* en = asEntity(e); return en ? en->scale : 1.0f; }
    Vec3 thunk_entityGetRotation(void* e) { Entity* en = asEntity(e); return en ? toScript(glm::degrees(glm::eulerAngles(en->rot))) : Vec3{ 0, 0, 0 }; }
    Vec3 thunk_entityGetForward(void* e) { Entity* en = asEntity(e); return en ? toScript(en->rot * glm::vec3(0, 0, -1)) : Vec3{ 0, 0, -1 }; }
    Vec3 thunk_entityGetRight(void* e) { Entity* en = asEntity(e); return en ? toScript(en->rot * glm::vec3(1, 0, 0)) : Vec3{ 1, 0, 0 }; }
    Vec3 thunk_entityGetUp(void* e) { Entity* en = asEntity(e); return en ? toScript(en->rot * glm::vec3(0, 1, 0)) : Vec3{ 0, 1, 0 }; }
    const char* thunk_entityGetName(void* e) { Entity* en = asEntity(e); return en ? en->displayName.c_str() : ""; }

    int thunk_entityGetEnabled(void* e)
    {
        Entity* en = asEntity(e);
        if (!en) return 1;
        SceneComponent* sc = getComponent<SceneComponent>(en);
        return sc ? (sc->enabled ? 1 : 0) : 1;
    }
    int thunk_entityGetChildCount(void* e)
    {
        Entity* en = asEntity(e);
        SceneComponent* sc = en ? getComponent<SceneComponent>(en) : nullptr;
        return sc ? (int)sc->children.size() : 0;
    }
    float thunk_entityGetBoundsRadius(void* e)
    {
        Entity* en = asEntity(e);
        RenderComponent* rc = en ? getComponent<RenderComponent>(en) : nullptr;
        return rc ? rc->node.getWorldBounds().radius : 0.0f;
    }

    void thunk_entitySetPosition(void* e, Vec3 v) { if (Entity* en = asEntity(e)) en->pos = glm::vec3(v.x, v.y, v.z); }
    void thunk_entitySetScale(void* e, float s) { if (Entity* en = asEntity(e)) en->scale = s; }
    void thunk_entitySetRotation(void* e, Vec3 d) { if (Entity* en = asEntity(e)) en->rot = glm::quat(glm::radians(glm::vec3(d.x, d.y, d.z))); }
    void thunk_entitySetEnabled(void* e, int enabled) { if (Entity* en = asEntity(e)) if (SceneComponent* sc = getComponent<SceneComponent>(en)) sc->enabled = enabled != 0; }
    void thunk_entitySetAnimFloat(void* e, const char* p, float v) { if (Entity* en = asEntity(e)) if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setFloat(p, v); }
    void thunk_entitySetAnimBool(void* e, const char* p, int v) { if (Entity* en = asEntity(e)) if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setBool(p, v != 0); }
    void thunk_entitySetAnimTrigger(void* e, const char* p) { if (Entity* en = asEntity(e)) if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setTrigger(p); }

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

