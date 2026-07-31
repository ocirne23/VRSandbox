module;

#include "ScriptAPI.h"

module Entity;

import Core;
import Core.Log;
import Core.Transform;
import :Entity;
import :ScriptContext;
import :ScriptEventManager;
import Script;

void ScriptComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
	enabled = info.enabled;
	initialValues = info.initialValues; // kept for re-application after any later reallocation

	if (info.scriptPath.empty())
		return;
    {
        const ScriptModule* loaded = Globals::scriptHost.getOrLoad(info.scriptPath);
        if (!loaded)
            return;

        // Logged once here; the refusal itself is requirementsMet(), re-checked at every entry point. Latching
        // it would outlive the edit that fixes it -- a hot-reload never re-runs spawn.
        if (const uint32 missing = loaded->requiredComponents & ~uint32(entity.typeBits))
        {
            std::string names;
            for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
                if (missing & (1u << i))
                    names += (names.empty() ? "" : ", ") + std::string(componentTypeName(EComponentID(i)));
            Log::error("Script '" + info.scriptPath + "' requires " + names + " -- entity '"
                + std::string(entity.getName()) + "' has none of that; it will not run until the entity gains"
                " those components or the script stops requiring them");
        }
        scriptModule = loaded;
    }

    syncScriptData(entity);

    if (scriptModule->onEvent)
        Globals::scriptEvents.registerListener(scriptModule, &entity, scriptData.get());

	// OnSpawn runs even for a frozen entity: it is the script's constructor, not a tick. Unmet //@@require
	// suppresses it -- a constructor is exactly where a script reaches for its components -- and update()
	// then runs it late, the first frame the entity does qualify.
	if (requirementsMet(entity))
	{
		onSpawnRan = true; // set even without an OnSpawn entry: it marks "this script has begun on this entity"
		if (scriptModule->onSpawn)
			reinterpret_cast<ScriptOnSpawnFn>(scriptModule->onSpawn)(&Globals::scriptContext, &entity, scriptData.get());
	}
}

// The block is REUSED while size and layout both match -- that is what makes a reload free for arrays: the
// handles survive, so the engine-side contents do. A mismatch releases the old arrays first (once the block
// goes nothing can reach those handles again) and clears onSpawnRan, so the script reconstructs into the
// fresh one. Runs at spawn AND live, since a hot-reload mutates the ScriptModule in place, never re-spawning.
bool ScriptComponent::syncScriptData(Entity& entity)
{
    if (!scriptModule || (scriptModule->dataSize == scriptDataSize && scriptModule->dataLayoutId == scriptDataLayoutId))
        return false;

    releaseScriptArrays(*this);
    scriptDataSize = scriptModule->dataSize;
    scriptDataLayoutId = scriptModule->dataLayoutId;
    scriptData = scriptDataSize ? std::make_unique<uint8[]>(scriptDataSize) : nullptr;
    onSpawnRan = false;

    // Required-component pointers (//@@require) occupy the FRONT of ScriptData, one 8-byte slot per set bit in
    // ascending EComponentID order, so "scriptData-><name>" reads an already-resolved pointer. EVERY id the
    // mask can carry needs a branch: a missing one shifts every later component into the wrong slot.
    if (scriptData && scriptModule->requiredComponents)
    {
        void** slot = reinterpret_cast<void**>(scriptData.get());
        if (scriptModule->requiredComponents & (1u << EComponentID_Scene))    *slot++ = getComponent<SceneComponent>(&entity);
        if (scriptModule->requiredComponents & (1u << EComponentID_Render))   *slot++ = getComponent<RenderComponent>(&entity);
        if (scriptModule->requiredComponents & (1u << EComponentID_Animator)) *slot++ = getComponent<AnimatorComponent>(&entity);
        if (scriptModule->requiredComponents & (1u << EComponentID_Physics))  *slot++ = getComponent<PhysicsComponent>(&entity);
        if (scriptModule->requiredComponents & (1u << EComponentID_Audio))    *slot++ = getComponent<AudioComponent>(&entity);
        if (scriptModule->requiredComponents & (1u << EComponentID_Particle)) *slot++ = getComponent<ParticleComponent>(&entity);
        if (scriptModule->requiredComponents & (1u << EComponentID_Force))    *slot++ = getComponent<ForceComponent>(&entity);
    }

    // The block was just zeroed, so the authored values have to go back in -- otherwise a hot-reload that
    // touched //@@data would silently reset every one of them. AFTER the require slots, which occupy the front
    // of the block and are never fields.
    applyInitialValues();
    return true;
}

// Text -> bytes, by each field's own type. Deliberately tolerant: a name that no longer exists in the script, or
// text that doesn't parse, is skipped rather than guessed at -- an authored value for a since-renamed field
// should quietly stop applying, never land on whatever field now occupies that offset.
void ScriptComponent::applyInitialValues()
{
    if (initialValues.empty() || scriptData == nullptr || scriptModule == nullptr
        || scriptModule->dataFields == nullptr || scriptModule->numDataFields <= 0)
        return;
    // ScriptModule carries the table as void* (it doesn't depend on the script ABI) -- typed once, here.
    const VrScriptField* fields = static_cast<const VrScriptField*>(scriptModule->dataFields);

    // Comma-separated floats for the vector/quat kinds, so an authored value reads the way every other vector
    // in the .pre format does ("1, 2, 3"). A short or unparseable list leaves the field alone.
    const auto parseFloats = [](const std::string& text, float* out, int count)
    {
        int parsed = 0;
        const char* p = text.c_str();
        while (parsed < count && *p != '\0')
        {
            char* end = nullptr;
            const float value = std::strtof(p, &end);
            if (end == p)
                break;
            out[parsed++] = value;
            p = end;
            while (*p == ',' || *p == ' ' || *p == '\t')
                ++p;
        }
        return parsed == count;
    };

    for (const InitialFieldValue& authored : initialValues)
    {
        const VrScriptField* field = nullptr;
        for (int i = 0; i < scriptModule->numDataFields && field == nullptr; ++i)
            if (authored.name == fields[i].name)
                field = &fields[i];
        if (field == nullptr || field->offset < 0 || uint32(field->offset) >= scriptDataSize)
            continue;
        uint8* slot = scriptData.get() + field->offset;

        switch (field->type)
        {
        case VR_FIELD_INT:   *reinterpret_cast<int*>(slot) = std::atoi(authored.value.c_str()); break;
        case VR_FIELD_FLOAT: *reinterpret_cast<float*>(slot) = std::strtof(authored.value.c_str(), nullptr); break;
        case VR_FIELD_BOOL:  *reinterpret_cast<bool*>(slot) = (authored.value == "true" || authored.value == "1"); break;
        // The block holds a const char* into ENGINE-interned storage, never into this component's own string --
        // the block outlives any particular InitialFieldValue (and a reload replaces the whole vector).
        case VR_FIELD_STRING:
            *reinterpret_cast<const char**>(slot) = Globals::scriptContext.internString(authored.value.c_str());
            break;
        case VR_FIELD_VEC2:  parseFloats(authored.value, reinterpret_cast<float*>(slot), 2); break;
        case VR_FIELD_VEC3:  parseFloats(authored.value, reinterpret_cast<float*>(slot), 3); break;
        case VR_FIELD_VEC4:
        case VR_FIELD_QUAT:  parseFloats(authored.value, reinterpret_cast<float*>(slot), 4); break;
        default: break;
        }
    }
}

// The live-path wrapper: the event manager holds the ScriptData pointer it was registered with, so a
// reallocation has to re-register or every fired event writes into freed memory.
void ScriptComponent::syncScriptDataLive(Entity& entity)
{
    if (syncScriptData(entity) && scriptModule->onEvent)
    {
        Globals::scriptEvents.unregisterListener(scriptModule, &entity);
        Globals::scriptEvents.registerListener(scriptModule, &entity, scriptData.get());
    }
}

bool ScriptComponent::requirementsMet(const Entity& entity) const
{
    return scriptModule == nullptr || (scriptModule->requiredComponents & ~uint32(entity.typeBits)) == 0;
}

void ScriptComponent::update(Entity& entity, float deltaSeconds)
{
    if (!enabled || !scriptModule)
        return;
    syncScriptDataLive(entity); // the module may have been recompiled under us since spawn
    if (!requirementsMet(entity))
        return;

    // Late construction: an entity that didn't satisfy the //@@require set at spawn -- or whose script has
    // since dropped the requirement it was missing -- runs OnSpawn the first frame it qualifies. Without this
    // a script fixed mid-session stays half-initialized on everything already spawned until it is respawned.
    if (!onSpawnRan)
    {
        onSpawnRan = true;
        if (scriptModule->onSpawn)
            reinterpret_cast<ScriptOnSpawnFn>(scriptModule->onSpawn)(&Globals::scriptContext, &entity, scriptData.get());
    }

    if (!scriptModule->update || entity.isFrozenInTree())
        return;
    reinterpret_cast<ScriptUpdateFn>(scriptModule->update)(&Globals::scriptContext, &entity, deltaSeconds, scriptData.get());
}

void ScriptComponent::fireEvent(Entity& entity, const std::string& eventName)
{
    if (!enabled || !scriptModule)
        return;
    syncScriptDataLive(entity); // the module may have been recompiled under us since spawn
    if (!scriptModule->onEvent || entity.isFrozenInTree() || !requirementsMet(entity))
        return;
    auto it = scriptModule->eventKeyToIndex.find(Globals::scriptEvents.findEventKey(eventName));
    if (it != scriptModule->eventKeyToIndex.end())
    {
        reinterpret_cast<ScriptOnEventFn>(scriptModule->onEvent)(&Globals::scriptContext, &entity, it->second, scriptData.get());
    }
}

void ScriptComponent::fireEvent(Entity& entity, uint32 eventKey)
{
    if (!enabled || !scriptModule)
        return;
    syncScriptDataLive(entity); // the module may have been recompiled under us since spawn
    if (!scriptModule->onEvent || entity.isFrozenInTree() || !requirementsMet(entity))
        return;
    auto it = scriptModule->eventKeyToIndex.find(eventKey);
    if (it != scriptModule->eventKeyToIndex.end())
    {
        reinterpret_cast<ScriptOnEventFn>(scriptModule->onEvent)(&Globals::scriptContext, &entity, it->second, scriptData.get());
    }
}

void ScriptComponent::firePhysicsEvent(Entity& entity, Entity* other, bool begin, bool sensor, int64 contactId)
{
    if (!enabled || !scriptModule)
        return;
    syncScriptDataLive(entity); // the module may have been recompiled under us since spawn
    if (!scriptModule->onPhysicsEvent || entity.isFrozenInTree() || !requirementsMet(entity))
        return;
    reinterpret_cast<ScriptOnPhysicsEventFn>(scriptModule->onPhysicsEvent)(
        &Globals::scriptContext, &entity, other, begin ? 1 : 0, sensor ? 1 : 0, contactId, scriptData.get());
}

void ScriptComponent::destroy(Entity& entity, const SpawnInfo& info)
{
    if (!scriptModule)
        return;

    // Paired with OnSpawn HAVING RUN, or a script edited mid-life tears down what it never built. The
    // requirement is checked on top: a skipped teardown beats reaching for a component it was refused
    // (the script's arrays are released below either way).
    if (scriptModule->onDestroy && onSpawnRan && requirementsMet(entity))
        reinterpret_cast<ScriptOnDestroyFn>(scriptModule->onDestroy)(&Globals::scriptContext, &entity, scriptData.get());

    if (scriptModule->onEvent)
        Globals::scriptEvents.unregisterListener(scriptModule, &entity);
}

// Arrays are released here rather than in destroy(): destroy() is the SCRIPT's teardown hook (it runs
// OnDestroy, which may still read them) and isn't reached on every path that frees a component.
ScriptComponent::~ScriptComponent()
{
    releaseScriptArrays(*this);
}

const ScriptComponent::SpawnInfo* getScriptSpawnInfo(const Entity* entity)
{
    if (!entity->spawnTemplate || !hasComponent<ScriptComponent>(entity))
        return nullptr;

    size_t idx = 0;
    for (uint16 i = 0; i < uint16(EComponentID_Script); ++i)
        if (entity->typeBits & (1 << i))
            ++idx;
    if (idx >= entity->spawnTemplate->spawnInfos.size())
        return nullptr;
    return static_cast<const ScriptComponent::SpawnInfo*>(entity->spawnTemplate->spawnInfos[idx].get());
}
