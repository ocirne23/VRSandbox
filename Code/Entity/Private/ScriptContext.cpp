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
import Core.Camera;
import RendererVK;
import Animation;
import Physics;
import Force;
import Spatial;
import Script;
import :Entity;
import :Component;
import :World;

// See the declaration in ScriptContext.ixx. `vec2`/`vec4` need no name of their own outside their own struct
// row (their self-referential member/function types spell DSLType::ThisBinding instead -- see structFor's
// callers and the DSL.ixx comment), but `vec3` is referenced well beyond it (self.pos, physics component
// functions, several Engine functions below), so its registerStruct return value is captured locally and reused
// throughout -- same for the three registerComponentType calls (physics/audio/force): each ALSO exposes itself
// as a self member as part of that one call (see ScriptBindings.ixx), so the return value only needs capturing
// here for the component's OWN BindingObject `type` field just below.
void registerScriptDslBindings()
{
    ScriptBindings& bindings = Globals::scriptBindings;
    using T = DSLType;

    const DSLType vec2 = bindings.registerStruct({ "vec2", "glm::vec2", { { "x", T::Float }, { "y", T::Float } }, "glm::vec2($1, $2)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
        },
        {
            { "length",     T::Float, {},                              "glm::length($r)" },
            { "normalized", T::ThisBinding, {},                        "glm::normalize($r)" },
            { "dot",        T::Float, { { "other", T::ThisBinding } }, "glm::dot($r, $1)" },
            { "distance",   T::Float, { { "other", T::ThisBinding } }, "glm::distance($r, $1)" },
        } });
    (void)(vec2);
    const DSLType vec3 = bindings.registerStruct({ "vec3", "glm::vec3", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float } }, "glm::vec3($1, $2, $3)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
            { "z", T::Float, "$r.z" },
        },
        {
            { "length",     T::Float, {},                              "glm::length($r)" },
            { "normalized", T::ThisBinding, {},                        "glm::normalize($r)" },
            { "dot",        T::Float, { { "other", T::ThisBinding } }, "glm::dot($r, $1)" },
            { "distance",   T::Float, { { "other", T::ThisBinding } }, "glm::distance($r, $1)" },
        } });
    const DSLType vec4 = bindings.registerStruct({ "vec4", "glm::vec4", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float }, { "w", T::Float } }, "glm::vec4($1, $2, $3, $4)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
            { "z", T::Float, "$r.z" },
            { "w", T::Float, "$r.w" },
        },
        {
            { "length",     T::Float, {},                              "glm::length($r)" },
            { "normalized", T::ThisBinding, {},                        "glm::normalize($r)" },
            { "dot",        T::Float, { { "other", T::ThisBinding } }, "glm::dot($r, $1)" },
            { "distance",   T::Float, { { "other", T::ThisBinding } }, "glm::distance($r, $1)" },
        } });
    (void)(vec4);
    const DSLType quat = bindings.registerStruct({ "quat", "glm::quat", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float }, { "w", T::Float } }, "glm::quat($4, $1, $2, $3)",
        {
            { "x", T::Float, "$r.x" },
            { "y", T::Float, "$r.y" },
            { "z", T::Float, "$r.z" },
            { "w", T::Float, "$r.w" },
        },
        {
            { "length",     T::Float, {},                              "glm::length($r)" },
            { "normalized", T::ThisBinding, {},                        "glm::normalize($r)" },
            { "dot",        T::Float, { { "other", T::ThisBinding } }, "glm::dot($r, $1)" },
        } });
    bindings.registerObject({ "self", T::Entity, /*sidebarTopLevel*/ true,
        {
            { "setEnabled",     T::Void,  { { "enabled", T::Bool } },                        "ctx->entitySetEnabled($r, $1)" },
            { "setAnimFloat",   T::Void,  { { "param", T::String }, { "value", T::Float } }, "ctx->entitySetAnimFloat($r, $1, $2)" },
            { "setAnimBool",    T::Void,  { { "param", T::String }, { "value", T::Bool } },  "ctx->entitySetAnimBool($r, $1, $2)" },
            { "setAnimTrigger", T::Void,  { { "param", T::String } },                        "ctx->entitySetAnimTrigger($r, $1)" },
            { "getChildCount",  T::Int,   {},                                                "ctx->entityGetChildCount($r)" },
            { "getBoundsRadius",T::Float, {},                                                "ctx->entityGetBoundsRadius($r)" },
        },
        {
            { "pos",     vec3,        "$r->pos" }, // self is Entity* -- a real field of the ABI mirror struct
            { "scale",   T::Float,    "$r->scale" },
            { "rot",     quat,        "$r->rot" },
            { "data",    T::ScriptData,        "(*(ScriptData*)scriptData)", /*writable*/ false },
            { "events",  T::ScriptEvents,      "$r",                         /*writable*/ false }, // special case in transpiler
        } });

    const DSLType physicsKind = bindings.registerComponentType("physics", "PhysicsComponent", "ctx->entityGetPhysicsComponent($r)");
    const DSLType audioKind = bindings.registerComponentType("audio", "AudioComponent", "ctx->entityGetAudioComponent($r)");
    const DSLType forceKind = bindings.registerComponentType("force", "ForceComponent", "ctx->entityGetForceComponent($r)");
    bindings.registerObject({ "physics", physicsKind, /*sidebarTopLevel*/ false,
        {
            { "getVelocity",  vec3,   {},                                          "ctx->physicsGetVelocity($r)" },
            { "setVelocity",  T::Void, { { "velocity", vec3 } },                   "ctx->physicsSetVelocity($r, $1)" },
            { "applyImpulse", T::Void, { { "impulse", vec3 } },                    "ctx->physicsApplyImpulse($r, $1)" },
            { "isAwake",      T::Bool, {},                                          "(ctx->physicsIsAwake($r) != 0)" },
            { "teleport",     T::Void, { { "position", vec3 }, { "eulerDeg", vec3 } }, "ctx->physicsTeleport($r, $1, $2)" },
        },
        {} });
    bindings.registerObject({ "audio", audioKind, /*sidebarTopLevel*/ false,
        {
            { "trigger", T::Void, { { "alias", T::String } }, "ctx->audioTrigger($r, self, $1, 0, glm::vec3(0.0f), 1.0f, 1.0f)" },
            { "stop",    T::Void, { { "alias", T::String } }, "ctx->audioStop($r, $1)" },
        },
        {} });
    bindings.registerObject({ "force", forceKind, /*sidebarTopLevel*/ false,
        {
            { "getOutput",   T::Float, {},                          "ctx->forceGetOutput($r)" },
            { "setOutput",   T::Void,  { { "output", T::Float } },  "ctx->forceSetOutput($r, $1)" },
            { "getReach",    T::Float, {},                          "ctx->forceGetReach($r)" },
            { "setReach",    T::Void,  { { "reach", T::Float } },   "ctx->forceSetReach($r, $1)" },
            { "setTeam",     T::Void,  { { "team", T::Int } },      "ctx->forceSetTeam($r, $1)" },
            { "getPressure", T::Float, {},                          "ctx->forceGetPressure($r)" },
        },
        {} });
    // free functions
    bindings.registerObject({ nullptr, T::Void, /*sidebarTopLevel*/ false,
        {
            { "print",           T::Void,  { { "message", T::String } },                                     "ctx->log($1)" }, // one plain string -- no {}-interpolation yet
            { "rayCast",         T::Float, { { "pos", vec3 }, { "dir", vec3 }, { "maxRayDist", T::Float } }, "ctx->physicsRayCastDistance($1, $2, $3)" },
            { "isKeyDown",       T::Bool,  { { "keyName", T::String } },                                     "(ctx->isKeyDown($1) != 0)" },
            { "sendEvent",       T::Void,  { { "eventName", T::String } },                                   "ctx->sendEvent($1)" },
            { "setSun",          T::Void,  { { "direction", vec3 }, { "color", vec3 }, { "intensity", T::Float } },                       "ctx->setSun($1, $2, $3)" },
            { "spawnPointLight", T::Void,  { { "position", vec3 }, { "range", T::Float }, { "color", vec3 }, { "intensity", T::Float } }, "ctx->spawnPointLight($1, $2, $3, $4)" },
        },
        {} });

    bindings.registerEntryPoint({ "OnSpawn",         {},                                            ", void* scriptData", "REGISTER_ON_SPAWN()" });
    bindings.registerEntryPoint({ "OnDestroy",       {},                                            ", void* scriptData", "REGISTER_ON_DESTROY()" });
    bindings.registerEntryPoint({ "Update",          { { "deltaSeconds", T::Float } },              ", float deltaSeconds, void* scriptData", "REGISTER_UPDATE()" });
    bindings.registerEntryPoint({ "OnEvent",         { { "eventIdx", T::Int } },                    ", int eventIdx, void* scriptData", "REGISTER_ON_EVENT()" });
    bindings.registerEntryPoint({ "OnPhysicsEvent",  { { "begin", T::Int }, { "sensor", T::Int } }, ", Entity* other, int begin, int sensor, long long contactId, void* scriptData", "REGISTER_ON_PHYSICS_EVENT()" });
}

// Implements the script ABI (ScriptAPI.h) against the engine and drives per-entity script execution.
// The Script library is a pure compiler/loader; this file owns the ScriptContext and all the thunks, so
// the renderer / SDL / animation dependencies live here in Entity (which already imports them) rather
// than leaking into Script.

// Scripts use a layout mirror of Entity (ScriptAPI.h, /DSCRIPT_BUILD) to read self->pos etc. directly. These
// assert the real engine layout matches that mirror Ã¢â‚¬â€ if Entity's leading members move, update both.
static_assert(offsetof(Entity, pos)    == 0,  "ScriptAPI.h Entity mirror out of sync: pos");
static_assert(offsetof(Entity, scale)  == 12, "ScriptAPI.h Entity mirror out of sync: scale");
static_assert(offsetof(Entity, rot)    == 16, "ScriptAPI.h Entity mirror out of sync: rot");
static_assert(offsetof(Entity, parent) == 32, "ScriptAPI.h Entity mirror out of sync: parent");

// The thunks have C linkage (external) so the cooked App-Scripts aggregate — a separate module — can call them
// by name (a plain C++ function here would have module linkage, invisible to it). The small helpers below ride
// along in the same block; their names are file-unique so the external linkage is harmless.
#pragma warning(push)
#pragma warning(disable: 4190) // extern "C" thunks return glm::vec3; harmless — same MSVC + glm on both sides of the ABI
extern "C"
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
    // va_list backing for logf, so the cooked build's inline logf method (which captures varargs into a va_list)
    // has something to forward to — you can't pass `...` straight through to another variadic function.
    void thunk_vlogf(const char* fmt, va_list ap)
    {
        if (!fmt) return;
        va_list ap2; va_copy(ap2, ap);
        const int n = std::vsnprintf(nullptr, 0, fmt, ap2); va_end(ap2);
        if (n < 0) return;
        std::string s(static_cast<size_t>(n) + 1, '\0');
        std::vsnprintf(&s[0], s.size(), fmt, ap);
        s.resize(static_cast<size_t>(n));
        Log::info(s);
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

    const char* thunk_entityGetName(Entity* e) { return e->getName(); }

    int thunk_entityGetEnabled(Entity* en) { return en->isEnabled() ? 1 : 0; }
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

    void thunk_entitySetEnabled(Entity* en, int enabled)              { en->setEnabled(enabled != 0); }
    void thunk_entitySetAnimFloat(Entity* en, const char* p, float v) { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setFloat(p, v); }
    void thunk_entitySetAnimBool(Entity * en, const char* p, int v)   { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setBool(p, v != 0); }
    void thunk_entitySetAnimTrigger(Entity* en, const char* p)        { if (AnimatorComponent* ac = getComponent<AnimatorComponent>(en)) ac->stateMachine.setTrigger(p); }

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

    // The DSL's single-expression rayCast(pos, dir, maxRayDist) -- wraps thunk_physicsRayCast's hit/miss +
    // fraction into one distance value: maxDist itself on a miss (never a sentinel outside the query range),
    // so e.g. `if rayCast(...) <= 0.1` reads correctly without a separate hit check.
    float thunk_physicsRayCastDistance(glm::vec3 origin, glm::vec3 dir, float maxDist)
    {
        const glm::vec3 translation = glm::normalize(dir) * maxDist;
        const PhysicsWorld::RayHit hit = Globals::physics.castRayClosest(origin, translation);
        return hit.hit ? hit.fraction * maxDist : maxDist;
    }

    int thunk_physicsContactGetPoint(long long contactId, glm::vec3* outPoint, glm::vec3* outNormal)
    {
        glm::vec3 point(0.0f), normal(0.0f);
        if (!Globals::physics.getContactPoint(contactId, point, normal))
            return 0;
        if (outPoint)  *outPoint = point;
        if (outNormal) *outNormal = normal;
        return 1;
    }

    int thunk_spatialQueryRadius(glm::vec3 position, float radius, Entity** outEntities, int maxOut)
    {
        if (!outEntities || maxOut <= 0)
            return 0;
        static std::vector<uint64> results; // main thread only, like every thunk
        Globals::spatialIndex.querySphere(glm::dvec3(position), radius, SpatialLayer_Render, results);
        const int count = glm::min(int(results.size()), maxOut);
        for (int i = 0; i < count; ++i)
            outEntities[i] = reinterpret_cast<Entity*>(results[i]);
        return count;
    }

    Entity* thunk_spatialGetNearestEntity(glm::vec3 position, float maxRadius, Entity* exclude)
    {
        return reinterpret_cast<Entity*>(Globals::spatialIndex.queryNearest(
            glm::dvec3(position), maxRadius, SpatialLayer_Render, reinterpret_cast<uint64>(exclude)));
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

    // ---- force field ---- entityGetForceComponent resolves the component once and returns it as an opaque
    // handle; the force* thunks take that handle and cast it straight back (asForce null-checks), so a chain of
    // get/sets skips the repeated getComponent lookup. Getters read the emitter's authored state / GPU
    // readbacks or the component's local placement fields; setters write them.
    void* thunk_entityGetForceComponent(Entity* en) { return en ? getComponent<ForceComponent>(en) : nullptr; }
    ForceComponent* asForce(void* p) { return static_cast<ForceComponent*>(p); }

    float thunk_forceGetOutput(void* p)       { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getOutput() : 0.0f; }
    float thunk_forceGetReach(void* p)        { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getReach() : 0.0f; }
    float thunk_forceGetFocus(void* p)        { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getFocus() : 0.5f; }
    float thunk_forceGetDistribution(void* p) { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getDistribution() : 0.5f; }
    float thunk_forceGetWidth(void* p)        { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getWidth() : 1.0f; }
    int   thunk_forceGetTeam(void* p)         { ForceComponent* fc = asForce(p); return fc ? int(fc->emitter.getTeam()) : 0; }
    glm::vec3 thunk_forceGetAppliedForce(void* p) { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getAppliedForce() : glm::vec3(0.0f); }
    float thunk_forceGetPressure(void* p)     { ForceComponent* fc = asForce(p); return fc ? fc->emitter.getPressure() : 0.0f; }
    glm::vec3 thunk_forceGetLocalDirection(void* p) { ForceComponent* fc = asForce(p); return fc ? fc->localDirection : glm::vec3(0.0f, 0.0f, -1.0f); }
    glm::vec3 thunk_forceGetLocalOffset(void* p)    { ForceComponent* fc = asForce(p); return fc ? fc->localOffset : glm::vec3(0.0f); }
    int   thunk_forceGetCentered(void* p)           { ForceComponent* fc = asForce(p); return (fc && fc->centered) ? 1 : 0; }

    void thunk_forceSetOutput(void* p, float v)       { if (ForceComponent* fc = asForce(p)) fc->emitter.setOutput(v); }
    void thunk_forceSetReach(void* p, float v)        { if (ForceComponent* fc = asForce(p)) fc->emitter.setReach(v); }
    void thunk_forceSetFocus(void* p, float v)        { if (ForceComponent* fc = asForce(p)) fc->emitter.setFocus(v); }
    void thunk_forceSetDistribution(void* p, float v) { if (ForceComponent* fc = asForce(p)) fc->emitter.setDistribution(v); }
    void thunk_forceSetWidth(void* p, float v)        { if (ForceComponent* fc = asForce(p)) fc->emitter.setWidth(v); }
    void thunk_forceSetTeam(void* p, int v)           { if (ForceComponent* fc = asForce(p)) fc->emitter.setTeam(uint32(glm::max(v, 0))); }
    void thunk_forceSetLocalOffset(void* p, glm::vec3 v) { if (ForceComponent* fc = asForce(p)) fc->localOffset = v; }
    void thunk_forceSetCentered(void* p, int v)       { if (ForceComponent* fc = asForce(p)) fc->centered = (v != 0); }

    void thunk_forceSetLocalDirection(void* p, glm::vec3 v)
    {
        if (ForceComponent* fc = asForce(p))
        {
            // Match spawn: store a unit axis so the per-frame follow doesn't scale the field direction.
            const float len2 = glm::dot(v, v);
            fc->localDirection = len2 > 1e-12f ? v * glm::inversesqrt(len2) : glm::vec3(0.0f, 0.0f, -1.0f);
        }
    }

    // ---- scene component ---- entityGetSceneComponent resolves the component once and returns it as an opaque
    // handle; the scene* thunks cast it back (asScene null-checks), so a chain of child reads/writes skips the
    // repeated getComponent lookup. Add/Remove recover the owning (parent) entity from the handle
    // (SceneComponent is component 0, always at a fixed offset from the Entity — see SceneComponent::getEntity).
    SceneComponent* asScene(void* p) { return static_cast<SceneComponent*>(p); }
    void* thunk_entityGetSceneComponent(Entity* en) { return en ? getComponent<SceneComponent>(en) : nullptr; }

    int thunk_sceneGetChildCount(void* p) { SceneComponent* sc = asScene(p); return sc ? (int)sc->children.size() : 0; }
    Entity* thunk_sceneFindChild(void* p, const char* name)
    {
        SceneComponent* sc = asScene(p);
        if (!sc || !name) return nullptr;
        for (const EntityPtr& child : sc->children)
            if (std::string_view(child->getName()) == name)
                return child.get();
        return nullptr;
    }
    Entity* thunk_sceneGetChildAt(void* p, int index)
    {
        SceneComponent* sc = asScene(p);
        if (sc && index >= 0 && index < (int)sc->children.size())
            return sc->children[index].get();
        return nullptr;
    }

    // Reparents child under the component's owner (the parent). If child was a root entity, the App-owned ref is
    // now stale — queued for removal so it isn't updated twice (see thunk_entityAddChild's original note).
    void thunk_sceneAddChild(void* p, Entity* child)
    {
        SceneComponent* sc = asScene(p);
        if (!sc || !child) return;
        Entity* parent = sc->getEntity();
        if (child->parent != parent) return;
        Globals::scriptEvents.addReparentRequest(EntityPtr(child), EntityPtr(parent));
    }

    // Detaches child from the component's owner, making it root again. Claim ownership before reparentEntity
    // (see thunk_entityRemoveChild's original note).
    void thunk_sceneRemoveChild(void* p, Entity* child)
    {
        SceneComponent* sc = asScene(p);
        if (!sc || !child) return;
        Entity* parent = sc->getEntity();
        if (child->parent != parent) return;
        Globals::scriptEvents.addReparentRequest(EntityPtr(child), EntityPtr(nullptr));
    }

    void thunk_sceneRemoveChildAt(void* p, int index)
    {
        thunk_sceneRemoveChild(p, thunk_sceneGetChildAt(p, index));
    }

    // ---- physics component ---- entityGetPhysicsComponent returns physicsOf (validity-checked) as an opaque
    // handle; the physics* thunks cast it back, skipping the repeated getComponent + body-valid lookup.
    void* thunk_entityGetPhysicsComponent(Entity* en) { return physicsOf(en); }

    int thunk_physicsIsAwake(void* p) { PhysicsComponent* pc = static_cast<PhysicsComponent*>(p); return (pc && pc->body.isAwake()) ? 1 : 0; }
    glm::vec3 thunk_physicsGetVelocity(void* p) { PhysicsComponent* pc = static_cast<PhysicsComponent*>(p); return pc ? pc->body.getLinearVelocity() : glm::vec3(0.0f); }
    void thunk_physicsSetVelocity(void* p, glm::vec3 v) { if (PhysicsComponent* pc = static_cast<PhysicsComponent*>(p)) pc->body.setLinearVelocity(v); }
    void thunk_physicsApplyImpulse(void* p, glm::vec3 v) { if (PhysicsComponent* pc = static_cast<PhysicsComponent*>(p)) pc->body.applyImpulse(v); }
    void thunk_physicsTeleport(void* p, glm::vec3 position, glm::vec3 eulerDeg)
    {
        PhysicsComponent* pc = static_cast<PhysicsComponent*>(p);
        if (!pc || pc->bodyType != EPhysicsBodyType::Dynamic)
            return; // kinematic/static bodies follow the entity; move those through the Entity mirror
        pc->body.setTransform(position, glm::quat(glm::radians(eulerDeg)));
    }

    // ---- audio component ---- entityGetAudioComponent returns the AudioComponent as an opaque handle; the
    // audio* thunks cast it back, skipping the repeated getComponent lookup. audioTrigger still needs the entity
    // so a spatial sound can follow it.
    void* thunk_entityGetAudioComponent(Entity* en) { return en ? getComponent<AudioComponent>(en) : nullptr; }

    void thunk_audioTrigger(void* p, Entity* en, const char* alias, int overrideMask, glm::vec3 position, float volume, float pitch)
    {
        AudioComponent* ac = static_cast<AudioComponent*>(p);
        if (!ac || !en || !alias)
            return;
        AudioComponent::TriggerOverrides overrides;
        if (overrideMask & 1) overrides.position = position;
        if (overrideMask & 2) overrides.volume = volume;
        if (overrideMask & 4) overrides.pitch = pitch;
        ac->trigger(*en, alias, overrides);
    }

    void thunk_audioStop(void* p, const char* alias)
    {
        if (AudioComponent* ac = static_cast<AudioComponent*>(p))
            ac->stopSound(alias ? alias : "");
    }

}
#pragma warning(pop)

// Binds the ABI function-pointer table to the engine thunks. Globals::scriptContext is constructed at
// startup, so the context is ready before any script runs. (Scripts never construct a ScriptContext, so
// their DLLs never need this definition.)
ScriptContext::ScriptContext()
    : log(&thunk_log)
    , logf(&thunk_logf)
    , isKeyDown(&thunk_isKeyDown)
    , spawnPointLight(&thunk_spawnPointLight)
    , setSun(&thunk_setSun)
    , spawnEntity(&thunk_spawnEntity)
    , destroyEntity(&thunk_destroyEntity)
    , entityGetName(&thunk_entityGetName)
    , entityGetEnabled(&thunk_entityGetEnabled)
    , entityGetChildCount(&thunk_entityGetChildCount)
    , entityGetBoundsRadius(&thunk_entityGetBoundsRadius)
    , entitySetEnabled(&thunk_entitySetEnabled)
    , entitySetAnimFloat(&thunk_entitySetAnimFloat)
    , entitySetAnimBool(&thunk_entitySetAnimBool)
    , entitySetAnimTrigger(&thunk_entitySetAnimTrigger)
    , physicsSetGravity(&thunk_physicsSetGravity)
    , physicsRayCast(&thunk_physicsRayCast)
    , physicsRayCastDistance(&thunk_physicsRayCastDistance)
    , sendEvent(&thunk_sendEvent)
    , sendEventToEntity(&thunk_sendEventToEntity)
    , physicsContactGetPoint(&thunk_physicsContactGetPoint)
    , spatialQueryRadius(&thunk_spatialQueryRadius)
    , spatialGetNearestEntity(&thunk_spatialGetNearestEntity)
    , entityGetForceComponent(&thunk_entityGetForceComponent)
    , forceGetOutput(&thunk_forceGetOutput)
    , forceGetReach(&thunk_forceGetReach)
    , forceGetFocus(&thunk_forceGetFocus)
    , forceGetDistribution(&thunk_forceGetDistribution)
    , forceGetWidth(&thunk_forceGetWidth)
    , forceGetTeam(&thunk_forceGetTeam)
    , forceGetAppliedForce(&thunk_forceGetAppliedForce)
    , forceGetPressure(&thunk_forceGetPressure)
    , forceGetLocalDirection(&thunk_forceGetLocalDirection)
    , forceGetLocalOffset(&thunk_forceGetLocalOffset)
    , forceGetCentered(&thunk_forceGetCentered)
    , forceSetOutput(&thunk_forceSetOutput)
    , forceSetReach(&thunk_forceSetReach)
    , forceSetFocus(&thunk_forceSetFocus)
    , forceSetDistribution(&thunk_forceSetDistribution)
    , forceSetWidth(&thunk_forceSetWidth)
    , forceSetTeam(&thunk_forceSetTeam)
    , forceSetLocalDirection(&thunk_forceSetLocalDirection)
    , forceSetLocalOffset(&thunk_forceSetLocalOffset)
    , forceSetCentered(&thunk_forceSetCentered)
    , entityGetSceneComponent(&thunk_entityGetSceneComponent)
    , sceneGetChildCount(&thunk_sceneGetChildCount)
    , sceneFindChild(&thunk_sceneFindChild)
    , sceneGetChildAt(&thunk_sceneGetChildAt)
    , sceneAddChild(&thunk_sceneAddChild)
    , sceneRemoveChild(&thunk_sceneRemoveChild)
    , sceneRemoveChildAt(&thunk_sceneRemoveChildAt)
    , entityGetPhysicsComponent(&thunk_entityGetPhysicsComponent)
    , physicsIsAwake(&thunk_physicsIsAwake)
    , physicsGetVelocity(&thunk_physicsGetVelocity)
    , physicsSetVelocity(&thunk_physicsSetVelocity)
    , physicsApplyImpulse(&thunk_physicsApplyImpulse)
    , physicsTeleport(&thunk_physicsTeleport)
    , entityGetAudioComponent(&thunk_entityGetAudioComponent)
    , audioTrigger(&thunk_audioTrigger)
    , audioStop(&thunk_audioStop)
{
    deltaSeconds = 0.0f;
    elapsedSeconds = 0.0f;
    cameraPosition = glm::vec3(0.0f);
    cameraDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    cameraFovDeg = 45.0f;
}

// Refreshes the per-frame fields; called once a frame (main.cpp) before any script runs this frame.
void ScriptContext::update(const Camera& camera, float newDeltaSeconds, float newElapsedSeconds)
{
    deltaSeconds = newDeltaSeconds;
    elapsedSeconds = newElapsedSeconds;

    // Forward/up are derived from the inverse view matrix (its -Z / Y columns).
    const glm::mat4 camToWorld = glm::inverse(camera.viewMatrix);
    cameraPosition = camera.position;
    cameraDirection = glm::normalize(-glm::vec3(camToWorld[2]));
    cameraUp = glm::normalize(glm::vec3(camToWorld[1]));
    cameraFovDeg = camera.fovDeg;
    cameraNear = camera.near;
    cameraFar = camera.far;
}