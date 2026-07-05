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
import Core.Transform;
import RendererVK;
import Animation;
import Physics;
import :Entity;
import :Component;
import :World;

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

    // Spawns immediately (so the caller can use the result right away, e.g. parent it or set its transform)
    // and hands the owning reference to App via scriptRootAdditions -- it isn't in `entities` yet, but the
    // Entity object itself already exists and is safe to read/write/reparent this same frame.
    Entity* thunk_spawnEntity(const char* assetPath, glm::vec3 position)
    {
        if (!assetPath) return nullptr;
        EntityPtr spawned = Globals::world.spawnAssetFile(assetPath, Transform(position, 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
        if (!spawned) return nullptr;
        Entity* raw = spawned.get();
		Globals::scriptEvents.addReparentRequest(std::move(spawned), EntityPtr(nullptr));
        return raw;
    }

    void thunk_destroyEntity(Entity* e)
    {
        Globals::scriptEvents.addDestroyRequest(EntityPtr(e));
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
    Entity* thunk_entityGetChildAt(Entity* en, int index)
    {
        if (SceneComponent* sc = getComponent<SceneComponent>(en))
            if (index >= 0 && index < (int)sc->children.size())
                return sc->children[index].get();
        return nullptr;
    }

    void thunk_entitySetEnabled(Entity* en, int enabled)              { if (SceneComponent* sc = getComponent<SceneComponent>(en)) sc->enabled = enabled != 0; }
    void thunk_entitySetAnimFloat(Entity* en, const char* p, float v) { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setFloat(p, v); }
    void thunk_entitySetAnimBool(Entity * en, const char* p, int v)   { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setBool(p, v != 0); }
    void thunk_entitySetAnimTrigger(Entity* en, const char* p)        { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setTrigger(p); }

    // Reparents child under parent. If child was previously a root entity (owned directly by App's `entities`
    // list rather than a SceneComponent), that ref is now stale -- queue it for removal so it isn't updated
    // twice (once via `entities`, once via parent's descendant chain).
    void thunk_entityAddChild(Entity* parent, Entity* child)
    {
        if (!child || child->parent != parent) return;
        Globals::scriptEvents.addReparentRequest(EntityPtr(child), EntityPtr(parent));
    }

    // Detaches child from parent, making it root again. Claim ownership (heap-boxed, see scriptRootAdditions)
    // before reparentEntity(nullptr) runs: it only guarantees the entity survives its own call, not any longer,
    // so without an external ref taken first the entity would be destroyed the instant it's detached.
    void thunk_entityRemoveChild(Entity* parent, Entity* child)
    {
        if (!child || child->parent != parent) return;
        Globals::scriptEvents.addReparentRequest(EntityPtr(child), EntityPtr(nullptr));
    }

    void thunk_entityRemoveChildAt(Entity* parent, int index)
    {
        thunk_entityRemoveChild(parent, thunk_entityGetChildAt(parent, index));
    }

    // ---- physics thunks ----

    PhysicsComponent* physicsOf(Entity* en)
    {
        if (!en)
            return nullptr;
        PhysicsComponent* pc = getComponent<PhysicsComponent>(en);
        return (pc && pc->body.isValid()) ? pc : nullptr;
    }

    void thunk_physicsSetGravity(glm::vec3 gravity) { Globals::physics.setGravity(gravity); }

    int thunk_physicsRayCast(glm::vec3 origin, glm::vec3 translation, glm::vec3* outPoint, glm::vec3* outNormal, float* outFraction)
    {
        const PhysicsWorld::RayHit hit = Globals::physics.castRayClosest(origin, translation);
        if (outPoint)    *outPoint = hit.point;
        if (outNormal)   *outNormal = hit.normal;
        if (outFraction) *outFraction = hit.fraction;
        return hit.hit ? 1 : 0;
    }

    int thunk_entityHasPhysics(Entity* en)     { return physicsOf(en) ? 1 : 0; }
    int thunk_entityIsPhysicsAwake(Entity* en) { PhysicsComponent* pc = physicsOf(en); return (pc && pc->body.isAwake()) ? 1 : 0; }

    glm::vec3 thunk_entityGetVelocity(Entity* en)
    {
        PhysicsComponent* pc = physicsOf(en);
        return pc ? pc->body.getLinearVelocity() : glm::vec3(0.0f);
    }

    void thunk_entitySetVelocity(Entity* en, glm::vec3 velocity)
    {
        if (PhysicsComponent* pc = physicsOf(en))
            pc->body.setLinearVelocity(velocity);
    }

    void thunk_entityApplyImpulse(Entity* en, glm::vec3 impulse)
    {
        if (PhysicsComponent* pc = physicsOf(en))
            pc->body.applyImpulse(impulse);
    }

    void thunk_entityTeleportPhysics(Entity* en, glm::vec3 position, glm::vec3 eulerDeg)
    {
        PhysicsComponent* pc = physicsOf(en);
        if (!pc || pc->bodyType != EPhysicsBodyType::Dynamic)
            return; // kinematic/static bodies follow the entity; move those through the Entity mirror
        pc->body.setTransform(position, glm::quat(glm::radians(eulerDeg)));
    }

    void thunk_sendEvent(const char* eventName)
    {
        if (eventName)
            Globals::scriptEvents.fireEvent(eventName); // broadcast to every listener of this event
    }
    void thunk_sendEventToEntity(Entity* en, const char* eventName)
    {
        if (!en || !eventName)
            return;
        if (ScriptComponent* sc = getComponent<ScriptComponent>(en))
            sc->fireEvent(*en, eventName); // targeted: only this entity's script
    }

    // ---- audio thunks ----

    void thunk_entityTriggerAudio(Entity* en, const char* alias, int overrideMask, glm::vec3 position, float volume, float pitch)
    {
        if (!en || !alias)
            return;
        AudioComponent* ac = getComponent<AudioComponent>(en);
        if (!ac)
            return;
        AudioComponent::TriggerOverrides overrides;
        if (overrideMask & 1) overrides.position = position;
        if (overrideMask & 2) overrides.volume = volume;
        if (overrideMask & 4) overrides.pitch = pitch;
        ac->trigger(*en, alias, overrides);
    }

    void thunk_entityStopAudio(Entity* en, const char* alias)
    {
        if (!en)
            return;
        if (AudioComponent* ac = getComponent<AudioComponent>(en))
            ac->stopSound(alias ? alias : "");
    }

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
    entityGetChildAt = &thunk_entityGetChildAt;
    entitySetEnabled = &thunk_entitySetEnabled;
    entitySetAnimFloat = &thunk_entitySetAnimFloat;
    entitySetAnimBool = &thunk_entitySetAnimBool;
    entitySetAnimTrigger = &thunk_entitySetAnimTrigger;
    entityAddChild = &thunk_entityAddChild;
    entityRemoveChild = &thunk_entityRemoveChild;
    entityRemoveChildAt = &thunk_entityRemoveChildAt;
    physicsSetGravity = &thunk_physicsSetGravity;
    physicsRayCast = &thunk_physicsRayCast;
    entityHasPhysics = &thunk_entityHasPhysics;
    entityIsPhysicsAwake = &thunk_entityIsPhysicsAwake;
    entityGetVelocity = &thunk_entityGetVelocity;
    entitySetVelocity = &thunk_entitySetVelocity;
    entityApplyImpulse = &thunk_entityApplyImpulse;
    entityTeleportPhysics = &thunk_entityTeleportPhysics;
    sendEvent = &thunk_sendEvent;
    sendEventToEntity = &thunk_sendEventToEntity;
    entityTriggerAudio = &thunk_entityTriggerAudio;
    entityStopAudio = &thunk_entityStopAudio;
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

