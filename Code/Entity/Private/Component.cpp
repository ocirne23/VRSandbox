module Entity;

import Core;
import Core.glm;
import Core.Log;
import Core.Transform;
import :Entity;
import :AnimationDescription;
import :AssetRegistry;
import :ObjectDescription;
import File;
import Animation;
import RendererVK;

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
    if (!info.desc || !info.skeleton)
        return;
    const AnimatorDesc& desc = *info.desc;

    player.initialize(info.skeleton);

    // Loads one .anm into this animator's set under `localName` (retargeted by bone name to the rig).
    auto loadClipDesc = [&](const AnimationClipDesc& anm, const std::string& localName)
    {
        std::string sourcePath = anm.source;
        if (const ObjectContainerDesc* oc = Globals::assetRegistry.findObjectContainer(anm.source))
            sourcePath = oc->path; // source named a registered container; use its file
        const char* skip = anm.skip.empty() ? nullptr : anm.skip.c_str();
        ISceneData::loadAnimations(sourcePath.c_str(), *info.skeleton, clips, skip, localName.c_str());
    };

    // Explicit `Clip <local> Anim <anm>` declarations are optional: they alias a clip to a local name (or
    // force-load one nothing references). Anything not declared is lazy-loaded by name below.
    for (const AnimatorDesc::ClipRef& ref : desc.clips)
    {
        if (const AnimationClipDesc* anm = Globals::assetRegistry.findClip(ref.anmName))
            loadClipDesc(*anm, ref.localName);
        else
            Log::warning("Animator '" + desc.name + "': unknown Animation clip '" + ref.anmName + "'");
    }

    // Resolve a clip by name, lazily loading it from the .anm registry when it wasn't declared with `Clip`.
    auto ensureClip = [&](const std::string& name)
    {
        if (name.empty() || clips.find(name))
            return;
        if (const AnimationClipDesc* anm = Globals::assetRegistry.findClip(name))
            loadClipDesc(*anm, name);
        else
            Log::warning("Animator '" + desc.name + "': unknown clip '" + name + "'");
    };

    // Pre-load every clip referenced by a blend sample or a state BEFORE building blend spaces / states,
    // since those capture pointers into `clips` (loading more afterwards would reallocate and dangle them).
    std::unordered_set<std::string> blendNames;
    for (const AnimatorDesc::BlendSpace& bs : desc.blendSpaces)
        blendNames.insert(bs.name);
    for (const AnimatorDesc::BlendSpace& bs : desc.blendSpaces)
        for (const AnimatorDesc::BlendSample& s : bs.samples)
            ensureClip(s.clip);
    for (const AnimatorDesc::State& st : desc.stateMachine.states)
        if (!blendNames.contains(st.play))
            ensureClip(st.play);

    player.setClipLibrary(&clips);

    // Blend spaces (reserve so the state machine can hold stable pointers into this vector).
    std::unordered_map<std::string, size_t> blendIndex;
    blendSpaces.reserve(desc.blendSpaces.size());
    for (const AnimatorDesc::BlendSpace& bs : desc.blendSpaces)
    {
        BlendSpace1D space;
        for (const AnimatorDesc::BlendSample& s : bs.samples)
            if (const AnimationClip* c = clips.find(s.clip))
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
            else if (const AnimationClip* c = clips.find(st.play))
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
    else if (clips.numClips() > 0)
    {
        player.play(clips.get(0)); // no state machine: just play the first clip
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

    if (RenderComponent* rc = getComponent<RenderComponent>(&entity); rc && rc->node.isSkinned())
        renderer.setSkinningPalette(rc->node.getSkinnedPaletteHandle(), player.getPalette());
}

void AnimatorComponent::destroy(Entity& entity, const SpawnInfo& info)
{
}

AnimatorComponent::~AnimatorComponent()
{
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

void writeAnimatorSpawnInfo(const AnimatorComponent::SpawnInfo& info, AssetNode& out)
{
    if (info.animatorName.empty())
        return;
    out.set("Animator", info.animatorName);
    if (!info.enabled)
        out.set("Enabled", info.enabled);
}

int componentIdFromName(std::string_view name)
{
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (name == componentTypeName(EComponentID(i)))
            return int(i);
    return -1;
}
