module;

#include "ScriptAPI.h"

module Entity;

import Core;
import Core.glm;
import Core.Log;
import Core.Transform;
import :Entity;
import :AnimationDescription;
import :ScriptContext;
import :ScriptEventManager;
import Animation;
import RendererVK;
import Script;
import Physics;
import Audio;

Transform composeTransform(const Transform& parent, const Transform& local)
{
    return Transform(
        parent.pos + parent.quat * (local.pos * parent.scale),
        parent.scale * local.scale,
        glm::normalize(parent.quat * local.quat));
}

void RenderComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    if (!info.container)
        return;
    localTransform = info.localTransform;
    const Transform world = composeTransform(base, info.localTransform);
    if (info.skinned && info.container->isSkinned())
        node = info.container->spawnSkinnedNode(world);
    else
        node = info.container->spawnNodeForIdx(info.nodeIdx, world);
}

void RenderComponent::destroy(Entity& entity, const SpawnInfo& info)
{

}

RenderComponent::~RenderComponent()
{
}

static AnimCondition toAnimCondition(const AnimatorDesc::Condition& c)
{
    using Op = AnimatorDesc::Condition::Op;
    switch (c.op)
    {
    case Op::Less:    return floatLess(c.param, c.value);
    case Op::Equal:   return boolIs(c.param, c.boolValue);
    case Op::Trigger: return trigger(c.param);
    case Op::Greater:
    default:          return floatGreater(c.param, c.value);
    }
}

void AnimatorComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    enabled = info.enabled;
    if (!info.desc || !info.skeleton || !info.clipSet)
        return;
    const AnimatorDesc& desc = *info.desc;
    clipSet = info.clipSet; // shared, World-cached library (clips already loaded + retargeted)

    player.initialize(info.skeleton);
    player.setClipLibrary(clipSet);

    // Blend spaces (reserve so the state machine can hold stable pointers into this vector).
    std::unordered_map<std::string, size_t> blendIndex;
    blendSpaces.reserve(desc.blendSpaces.size());
    for (const AnimatorDesc::BlendSpace& bs : desc.blendSpaces)
    {
        BlendSpace1D space;
        for (const AnimatorDesc::BlendSample& s : bs.samples)
            if (const AnimationClip* c = clipSet->find(s.clip))
                space.addSample(c, s.position);
        blendIndex[bs.name] = blendSpaces.size();
        blendSpaces.push_back(std::move(space));
    }

    defaultSpeed = desc.speed;

    if (desc.stateMachine.present)
    {
        hasStateMachine = true;
        stateMachine.initialize(&player);

        for (const AnimatorDesc::Param& p : desc.params)
        {
            if (p.type == AnimatorDesc::ParamType::Float) stateMachine.setFloat(p.name, p.floatValue);
            else if (p.type == AnimatorDesc::ParamType::Bool) stateMachine.setBool(p.name, p.boolValue);
        }

        std::unordered_map<std::string, AnimStateMachine::StateId> stateIds;
        for (const AnimatorDesc::State& st : desc.stateMachine.states)
        {
            AnimStateMachine::StateId id = AnimStateMachine::INVALID_STATE;
            if (auto it = blendIndex.find(st.play); it != blendIndex.end())
                id = stateMachine.addBlendState(st.name, &blendSpaces[it->second], desc.blendSpaces[it->second].axisParam);
            else if (const AnimationClip* c = clipSet->find(st.play))
                id = stateMachine.addClipState(st.name, c);
            else
            {
                Log::warning("Animator '" + desc.name + "': state '" + st.name + "' plays unknown source '" + st.play + "'");
                continue;
            }
            stateIds[st.name] = id;
            stateSpeeds.push_back(st.speed); // StateIds are sequential in add order, so this aligns by index
        }

        if (auto it = stateIds.find(desc.stateMachine.entry); it != stateIds.end())
            stateMachine.setEntryState(it->second);

        for (const AnimatorDesc::Transition& tr : desc.stateMachine.transitions)
        {
            std::vector<AnimCondition> conds;
            conds.reserve(tr.conditions.size());
            for (const AnimatorDesc::Condition& c : tr.conditions)
                conds.push_back(toAnimCondition(c));

            const auto toIt = stateIds.find(tr.to);
            if (toIt == stateIds.end())
                continue;
            if (tr.from.empty())
            {
                stateMachine.addAnyTransition(toIt->second, std::move(conds), tr.fade, tr.exitTime);
            }
            else if (const auto fromIt = stateIds.find(tr.from); fromIt != stateIds.end())
            {
                stateMachine.addTransition(fromIt->second, toIt->second, std::move(conds), tr.fade, tr.exitTime);
            }
        }
    }
    else if (clipSet->numClips() > 0)
    {
        player.play(clipSet->get(0)); // no state machine: just play the first clip
        if (defaultSpeed.hasConst)
            player.setSpeed(defaultSpeed.value); // no parameters without a state machine, so const only
    }

    built = true;
}

float AnimatorComponent::resolvePlaybackSpeed() const
{
    AnimStateMachine::StateId cur = stateMachine.getCurrentState();
    const AnimatorDesc::SpeedBinding* b =
        (cur != AnimStateMachine::INVALID_STATE && cur < stateSpeeds.size() && stateSpeeds[cur].isSet())
        ? &stateSpeeds[cur]
        : (defaultSpeed.isSet() ? &defaultSpeed : nullptr);
    if (!b)
        return 1.0f;
    if (!b->param.empty())
        return stateMachine.getFloat(b->param) * b->scale;
    return b->value;
}

void AnimatorComponent::update(Entity& entity, Renderer& renderer, float deltaSeconds)
{
    if (!enabled || !built)
        return;
    if (hasStateMachine)
    {
        stateMachine.update(deltaSeconds); // fires transitions + sets the blend axis (does not tick)
        player.setSpeed(resolvePlaybackSpeed()); // warp playback rate for the active state
    }
    player.tick(deltaSeconds);

    if (!player.getFiredEvents().empty())
    {
        for (const std::string& e : player.getFiredEvents()) // notifies crossed this tick
        {
            if (onEvent) onEvent(e);
        }
    }

    if (RenderComponent* rc = getComponent<RenderComponent>(&entity); rc && rc->node.isSkinned())
        renderer.setSkinningPalette(rc->node.getSkinnedPaletteHandle(), player.getPalette());
}

void AnimatorComponent::destroy(Entity& entity, const SpawnInfo& info)
{
}

AnimatorComponent::~AnimatorComponent()
{
}

void ScriptComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
	enabled = info.enabled;

	if (info.scriptPath.empty())
		return;
    {
        const ScriptModule* loaded = Globals::scriptHost.getOrLoad(info.scriptPath);
        if (!loaded)
            return;
        scriptModule = loaded;
    }

    if (scriptModule->dataSize != scriptDataSize)
    {
        scriptDataSize = scriptModule->dataSize;
        scriptData = scriptDataSize ? std::make_unique<uint8[]>(scriptDataSize) : nullptr;
    }

    if (scriptModule->onEvent)
        Globals::scriptEvents.registerListener(scriptModule, &entity, scriptData.get());

	if (scriptModule->onSpawn)
		reinterpret_cast<ScriptOnSpawnFn>(scriptModule->onSpawn)(&Globals::scriptContext, &entity, scriptData.get());
}

void ScriptComponent::update(Entity& entity, float deltaSeconds)
{
    if (!enabled || !scriptModule || !scriptModule->update)
        return;
    reinterpret_cast<ScriptUpdateFn>(scriptModule->update)(&Globals::scriptContext, &entity, deltaSeconds, scriptData.get());
}

void ScriptComponent::fireEvent(Entity& entity, const std::string& eventName)
{
    if (!enabled || !scriptModule || !scriptModule->onEvent)
        return;
    auto it = scriptModule->eventKeyToIndex.find(Globals::scriptEvents.getEventKeyForName(eventName));
    if (it != scriptModule->eventKeyToIndex.end())
    {
        reinterpret_cast<ScriptOnEventFn>(scriptModule->onEvent)(&Globals::scriptContext, &entity, it->second, scriptData.get());
    }
}

void ScriptComponent::fireEvent(Entity& entity, uint32 eventKey)
{
    if (!enabled || !scriptModule || !scriptModule->onEvent)
        return;
    auto it = scriptModule->eventKeyToIndex.find(eventKey);
    if (it != scriptModule->eventKeyToIndex.end())
    {
        reinterpret_cast<ScriptOnEventFn>(scriptModule->onEvent)(&Globals::scriptContext, &entity, it->second, scriptData.get());
    }
}

void ScriptComponent::firePhysicsEvent(Entity& entity, Entity* other, bool begin, bool sensor, int64 contactId)
{
    if (!enabled || !scriptModule || !scriptModule->onPhysicsEvent)
        return;
    reinterpret_cast<ScriptOnPhysicsEventFn>(scriptModule->onPhysicsEvent)(
        &Globals::scriptContext, &entity, other, begin ? 1 : 0, sensor ? 1 : 0, contactId, scriptData.get());
}

void ScriptComponent::destroy(Entity& entity, const SpawnInfo& info)
{
    if (!scriptModule)
        return;

    if (scriptModule->onDestroy)
        reinterpret_cast<ScriptOnDestroyFn>(scriptModule->onDestroy)(&Globals::scriptContext, &entity, scriptData.get());

    if (scriptModule->onEvent)
        Globals::scriptEvents.unregisterListener(scriptModule, &entity);
}

void PhysicsComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    enabled = info.enabled;
    bodyType = info.bodyType;
    lastWorld = base;
    prevPos = currPos = base.pos;
    prevRot = currRot = base.quat;
    lastStep = Globals::physics.getStepCount();

    PhysicsBodyDesc desc;
    desc.type = info.bodyType;
    desc.transform = base; // may be parent-local for child spawns; corrected on the first update
    desc.userData = &entity;
    body = Globals::physics.createBody(desc, std::span(&info.shape, 1));
}

void PhysicsComponent::destroy(Entity& entity, const SpawnInfo& info)
{
}

void PhysicsComponent::update(Entity& entity, const Transform& parentWorld)
{
    if (!body.isValid())
        return;

    const Transform world = composeTransform(parentWorld, Transform(entity.pos, entity.scale, entity.rot));
    if (!synced)
    {
        body.setTransform(world.pos, world.quat);
        lastWorld = world;
        prevPos = currPos = world.pos;
        prevRot = currRot = world.quat;
        synced = true;
        return;
    }
    if (!enabled)
        return;

    if (bodyType == EPhysicsBodyType::Dynamic)
    {
        // Track the pose per physics step so rendering can interpolate between fixed steps.
        const uint32 stepCount = Globals::physics.getStepCount();
        if (stepCount != lastStep)
        {
            prevPos = currPos;
            prevRot = currRot;
            currPos = body.getPosition();
            currRot = body.getRotation();
            lastStep = stepCount;
        }
        const float alpha = Globals::physics.getInterpolationAlpha();
        const glm::vec3 pos = glm::mix(prevPos, currPos, alpha);
        const glm::quat rot = glm::slerp(prevRot, currRot, alpha);
        const Transform local = parentWorld.inverse() * Transform(pos, world.scale, rot);
        entity.pos = local.pos;
        entity.rot = local.quat;
    }
    else if (world.pos != lastWorld.pos || world.quat != lastWorld.quat)
    {
        body.setTransform(world.pos, world.quat); // entity was moved (gizmo/script); the body follows
        lastWorld = world;
    }
}

void dispatchPhysicsContactEvents()
{
    for (const PhysicsWorld::ContactEvent& evt : Globals::physics.getContactEvents())
    {
        Entity* a = static_cast<Entity*>(evt.userDataA);
        Entity* b = static_cast<Entity*>(evt.userDataB);
        auto fire = [&](Entity* target, Entity* other)
        {
            if (!target)
                return; // bodies created outside the ECS (ground plane, world static body) have no entity
            if (PhysicsComponent* pc = getComponent<PhysicsComponent>(target); pc && pc->onContact && other)
                pc->onContact(*other, evt.begin);
            if (ScriptComponent* sc = getComponent<ScriptComponent>(target))
                sc->firePhysicsEvent(*target, other, evt.begin, evt.sensor, evt.contactId);
        };
        fire(a, b);
        fire(b, a);
    }
}

void SceneComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    enabled = info.enabled;
    for (const SpawnInfo::ChildSpawnInfo& child : info.children)
    {
        if (!child.tmpl)
            continue;
        EntityPtr childEntity = Entity::create(*child.tmpl, child.localTransform);
        if (!child.name.empty())
            childEntity->displayName = child.name;
        childEntity->reparentEntity(&entity); // hands the owning child handle to this entity's children list
    }
}

void SceneComponent::destroy(Entity& entity, const SpawnInfo& info)
{

}

static void detachFromParent(Entity* parent, Entity* child)
{
    SceneComponent* psc = getComponent<SceneComponent>(parent);
    if (!psc)
        return;
    auto& kids = psc->children;
    auto it = std::find_if(kids.begin(), kids.end(),
        [child](const EntityPtr& p) { return p.get() == child; });
    if (it != kids.end())
        kids.erase(it);
}

void detachFromOwner(Entity* child)
{
    if (child->parent)
        detachFromParent(child->parent, child);
}

const RenderComponent::SpawnInfo* getRenderSpawnInfo(const Entity* entity)
{
    if (!entity->spawnTemplate || !hasComponent<RenderComponent>(entity))
        return nullptr;

    size_t idx = 0;
    for (uint16 i = 0; i < uint16(EComponentID_Render); ++i)
        if (entity->typeBits & (1 << i))
            ++idx;
    if (idx >= entity->spawnTemplate->spawnInfos.size())
        return nullptr;
    return static_cast<const RenderComponent::SpawnInfo*>(entity->spawnTemplate->spawnInfos[idx].get());
}

const AnimatorComponent::SpawnInfo* getAnimatorSpawnInfo(const Entity* entity)
{
    if (!entity->spawnTemplate || !hasComponent<AnimatorComponent>(entity))
        return nullptr;

    size_t idx = 0;
    for (uint16 i = 0; i < uint16(EComponentID_Animator); ++i)
        if (entity->typeBits & (1 << i))
            ++idx;
    if (idx >= entity->spawnTemplate->spawnInfos.size())
        return nullptr;
    return static_cast<const AnimatorComponent::SpawnInfo*>(entity->spawnTemplate->spawnInfos[idx].get());
}

void AudioComponent::spawn(Entity& entity, const SpawnInfo& spawnInfo, const Transform& base)
{
    info = &spawnInfo;
    voices.resize(spawnInfo.sounds.size());
}

void AudioComponent::destroy(Entity& entity, const SpawnInfo&)
{
    voices.clear(); // AudioSource RAII stops + releases the playing sounds
}

int AudioComponent::findSound(std::string_view alias) const
{
    if (!info)
        return -1;
    for (int i = 0; i < (int)info->sounds.size(); ++i)
        if (info->sounds[i].alias == alias)
            return i;
    return -1;
}

const char* audioSelectToken(EAudioSelect select)
{
    switch (select)
    {
    case EAudioSelect::Random:           return "Random";
    case EAudioSelect::RandomNoRepeat:   return "RandomNoRepeat";
    case EAudioSelect::Cycle:            return "Cycle";
    case EAudioSelect::CycleStartRandom: return "CycleStartRandom";
    case EAudioSelect::Single:
    default:                             return "Single";
    }
}

EAudioSelect audioSelectFromToken(std::string_view token)
{
    if (token == "Random")           return EAudioSelect::Random;
    if (token == "RandomNoRepeat")   return EAudioSelect::RandomNoRepeat;
    if (token == "Cycle")            return EAudioSelect::Cycle;
    if (token == "CycleStartRandom") return EAudioSelect::CycleStartRandom;
    return EAudioSelect::Single;
}

// The entity's world position, composed on demand (updateTree computes world transforms transiently).
static glm::vec3 worldPositionOf(const Entity& entity)
{
    Transform world(entity.pos, entity.scale, entity.rot);
    for (const Entity* p = entity.parent; p; p = p->parent)
        world = composeTransform(Transform(p->pos, p->scale, p->rot), world);
    return world.pos;
}

// Picks the clip to play for this trigger by the sound's Select mode, advancing the voice's selection
// state. RandomNoRepeat draws uniformly from the clips other than the last one played.
int AudioComponent::selectClip(const SoundDesc& sound, Voice& voice) const
{
    const int count = (int)sound.clips.size();
    if (count <= 1)
        return 0;
    switch (sound.select)
    {
    case EAudioSelect::Cycle:
    {
        if (voice.cycleNext == -1)
            voice.cycleNext = 0;
        const int idx = int(voice.cycleNext % uint32(count));
        voice.cycleNext = (voice.cycleNext + 1) % uint32(count);
        return idx;
    }
    case EAudioSelect::CycleStartRandom:
    {
        if (voice.cycleNext == -1)
            voice.cycleNext = glm::min(int(glm::linearRand(0.0f, float(count))), count - 1);
        const int idx = int(voice.cycleNext % uint32(count));
        voice.cycleNext = (voice.cycleNext + 1) % uint32(count);
        return idx;
    }
    case EAudioSelect::RandomNoRepeat:
    {
        if (voice.lastClip < 0)
            return glm::min(int(glm::linearRand(0.0f, float(count))), count - 1);
        int idx = glm::min(int(glm::linearRand(0.0f, float(count - 1))), count - 2); // pick among the other clips
        if (idx >= voice.lastClip)
            ++idx;
        return idx;
    }
    case EAudioSelect::Random:
    default:
        return glm::min(int(glm::linearRand(0.0f, float(count))), count - 1);
    }
}

bool AudioComponent::trigger(Entity& entity, std::string_view alias, const TriggerOverrides& overrides)
{
    const int idx = findSound(alias);
    if (idx < 0)
    {
        Log::warning("Audio: entity '" + entity.displayName + "' has no sound named '" + std::string(alias) + "'");
        return false;
    }
    const SoundDesc& sound = info->sounds[idx];
    if (sound.clips.empty())
        return false;
    Voice& voice = voices[idx];
    const int clipIdx = selectClip(sound, voice);
    const Clip& clip = sound.clips[clipIdx];
    if (!clip.buffer || !clip.buffer->isValid())
        return false; // load failure already logged by World
    if (!voice.source.isValid())
    {
        voice.source = Globals::audio.createSource();
        if (!voice.source.isValid())
            return false;
    }
    if (voice.currentClip != clipIdx) // reselect the buffer only when the chosen clip changes
    {
        voice.source.setBuffer(*clip.buffer);
        voice.currentClip = clipIdx;
    }
    voice.source.setLooping(clip.loop);
    voice.source.setGain(overrides.volume.value_or(clip.volume));
    voice.source.setPitch(overrides.pitch.value_or(clip.pitch));
    voice.source.setRelative(clip.relative);
    voice.source.setAttenuation(clip.referenceDistance, clip.maxDistance, clip.rolloff);
    voice.follow = !overrides.position.has_value();
    if (voice.follow)
        voice.source.setPosition(worldPositionOf(entity));
    else
        voice.source.setPosition(overrides.position.value());
    voice.source.play();
    voice.lastClip = clipIdx;
    return true;
}

void AudioComponent::stopSound(std::string_view alias)
{
    for (int i = 0; i < (int)voices.size(); ++i)
        if ((alias.empty() || (info && info->sounds[i].alias == alias)) && voices[i].source.isValid())
            voices[i].source.stop();
}

void AudioComponent::update(Entity& entity, const Transform& world)
{
    for (Voice& voice : voices)
        if (voice.follow && voice.source.isValid() && voice.source.isPlaying())
            voice.source.setPosition(world.pos);
}

const PhysicsComponent::SpawnInfo* getPhysicsSpawnInfo(const Entity* entity)
{
    if (!entity->spawnTemplate || !hasComponent<PhysicsComponent>(entity))
        return nullptr;

    size_t idx = 0;
    for (uint16 i = 0; i < uint16(EComponentID_Physics); ++i)
        if (entity->typeBits & (1 << i))
            ++idx;
    if (idx >= entity->spawnTemplate->spawnInfos.size())
        return nullptr;
    return static_cast<const PhysicsComponent::SpawnInfo*>(entity->spawnTemplate->spawnInfos[idx].get());
}

void writePhysicsSpawnInfo(const PhysicsComponent::SpawnInfo& info, AssetNode& out)
{
    switch (info.bodyType)
    {
    case EPhysicsBodyType::Static:    out.set("Body", "Static");    break;
    case EPhysicsBodyType::Kinematic: out.set("Body", "Kinematic"); break;
    case EPhysicsBodyType::Dynamic:   out.set("Body", "Dynamic");   break;
    }
    const PhysicsShape& shape = info.shape;
    const PhysicsShape defaults;
    switch (shape.type)
    {
    case EPhysicsShapeType::Box:
        out.set("Shape", "Box");
        out.set("HalfExtents", shape.halfExtents);
        break;
    case EPhysicsShapeType::Sphere:
        out.set("Shape", "Sphere");
        out.set("Radius", shape.radius);
        break;
    case EPhysicsShapeType::Capsule:
        out.set("Shape", "Capsule");
        out.set("Radius", shape.radius);
        out.set("HalfHeight", shape.halfHeight);
        break;
    case EPhysicsShapeType::Hull:
        out.set("Shape", "Hull"); // point cloud re-derived from the render mesh on load
        if (shape.maxHullVertices != defaults.maxHullVertices)
            out.addChild("MaxHullVertices").values.emplace_back(std::to_string(shape.maxHullVertices));
        break;
    case EPhysicsShapeType::Mesh:
        out.set("Shape", "Mesh"); // BVH re-derived from the render mesh on load
        break;
    }
    if (shape.isSensor)      out.set("Sensor", shape.isSensor);
    if (shape.contactEvents) out.set("ContactEvents", shape.contactEvents);
    if (shape.offset != defaults.offset)           out.set("Offset", shape.offset);
    if (shape.density != defaults.density)         out.set("Density", shape.density);
    if (shape.friction != defaults.friction)       out.set("Friction", shape.friction);
    if (shape.restitution != defaults.restitution) out.set("Restitution", shape.restitution);
    if (!info.layer.empty())                       out.set("Layer", info.layer);
    if (!info.collidesWith.empty())                out.addChild("CollidesWith").values = info.collidesWith;
    if (shape.groupIndex != 0)                     out.addChild("Group").values.emplace_back(std::to_string(shape.groupIndex));
    if (!info.enabled)                             out.set("Enabled", info.enabled);
}

void writeAnimatorSpawnInfo(const AnimatorComponent::SpawnInfo& info, AssetNode& out)
{
    if (info.animatorName.empty())
        return;
    out.set("Animator", info.animatorName);
    if (!info.enabled)
        out.set("Enabled", info.enabled);
}

const AudioComponent::SpawnInfo* getAudioSpawnInfo(const Entity* entity)
{
    if (!entity->spawnTemplate || !hasComponent<AudioComponent>(entity))
        return nullptr;

    size_t idx = 0;
    for (uint16 i = 0; i < uint16(EComponentID_Audio); ++i)
        if (entity->typeBits & (1 << i))
            ++idx;
    if (idx >= entity->spawnTemplate->spawnInfos.size())
        return nullptr;
    return static_cast<const AudioComponent::SpawnInfo*>(entity->spawnTemplate->spawnInfos[idx].get());
}

void writeAudioSpawnInfo(const AudioComponent::SpawnInfo& info, AssetNode& out)
{
    const AudioComponent::Clip defaults;
    for (const AudioComponent::SoundDesc& sound : info.sounds)
    {
        AssetNode& soundNode = out.addChild("Sound");
        soundNode.values.emplace_back(sound.alias);
        if (sound.select != EAudioSelect::Single)
            soundNode.set("Select", audioSelectToken(sound.select));
        for (const AudioComponent::Clip& clip : sound.clips)
        {
            AssetNode& pathNode = soundNode.addChild("Path");
            pathNode.values.emplace_back(clip.path);
            if (clip.volume != defaults.volume)     pathNode.set("Volume", clip.volume);
            if (clip.pitch != defaults.pitch)       pathNode.set("Pitch", clip.pitch);
            if (clip.loop != defaults.loop)         pathNode.set("Loop", clip.loop);
            if (clip.relative != defaults.relative) pathNode.set("Relative", clip.relative);
            if (clip.referenceDistance != defaults.referenceDistance) pathNode.set("ReferenceDistance", clip.referenceDistance);
            if (clip.maxDistance != defaults.maxDistance)             pathNode.set("MaxDistance", clip.maxDistance);
            if (clip.rolloff != defaults.rolloff)                     pathNode.set("Rolloff", clip.rolloff);
        }
    }
}

int componentIdFromName(std::string_view name)
{
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (name == componentTypeName(EComponentID(i)))
            return int(i);
    return -1;
}
