module Entity;

import Core;
import Core.glm;
import Core.Sphere;
import Core.Transform;
import File;
import :Component;
import :Allocator;

import RendererVK;
import Spatial;

EntityArchetype makeEntityArchetype(uint16 typeBits)
{
    return EntityArchetype{ uint16(getEntityAllocSize(typeBits)), typeBits };
}

void Entity::updateTree(Renderer& renderer, const Transform& parentWorld, float deltaSeconds, bool editorPaused)
{
    SceneComponent* sc = getComponent<SceneComponent>(this);
    if (sc && !sc->enabled)
    {
        // The pruned subtree's physics bodies would keep colliding invisibly: pull them from the
        // simulation once. Each body resyncs and re-enables itself in its next update() after re-enable.
        if (!sc->physicsSuspended)
        {
            suspendPhysicsTree(*this);
            sc->physicsSuspended = true;
        }
        return;
    }
    if (sc)
        sc->physicsSuspended = false;

    editorPaused = editorPaused || isEditorPaused();

    if (!editorPaused)
        if (ScriptComponent* script = getComponent<ScriptComponent>(this))
            script->update(*this, deltaSeconds);

    if (AnimatorComponent* animator = getComponent<AnimatorComponent>(this))
        animator->update(*this, renderer, deltaSeconds); // advance animation + refresh skinning palette

    if (!editorPaused)
        if (PhysicsComponent* physics = getComponent<PhysicsComponent>(this))
            physics->update(*this, parentWorld); // dynamic bodies write the simulated pose into pos/rot

    const Transform world = composeTransform(parentWorld, Transform(pos, scale, rot));
    if (RenderComponent* render = getComponent<RenderComponent>(this))
    {
        if (render->node.isValid()) // empty when spawned without a container, or after destroy()
        {
            render->node.setTransform(composeTransform(world, render->localTransform));
            SpatialIndex& spatialIndex = Globals::spatialIndex;
            const SpatialCullingConfig& culling = spatialIndex.getCullingConfig();
            if (render->spatialEntry.isValid())
            {
                const Sphere bounds = render->node.getWorldBounds();
                const float radius = render->node.isSkinned() ? bounds.radius * culling.skinnedRadiusScale : bounds.radius;
                spatialIndex.updateEntry(render->spatialEntry.handle(), glm::dvec3(bounds.pos), radius);
            }
            if (culling.mode >= int(ESpatialCullMode::Cull) && render->spatialEntry.isValid())
            {
                const uint32 spatialMask = spatialIndex.getPassMask(render->spatialEntry.handle());
                uint32 passMask = 0;
                if (spatialMask & SpatialPassBit_Main)
                    passMask = RendererVKLayout::PASS_ALL; // in view: feeds every pass
                else if ((spatialMask & SpatialPassBit_Near) && culling.mode != int(ESpatialCullMode::MainOnly))
                    passMask = RendererVKLayout::PASS_SHADOW | RendererVKLayout::PASS_GI; // off-screen but shadow/RT relevant
                if (passMask != 0)
                    renderer.renderNode(render->node, passMask);
            }
            else
                renderer.renderNode(render->node);
        }
    }

    if (AudioComponent* audio = getComponent<AudioComponent>(this))
        audio->update(*this, world); // playing follow-sounds track the entity

    if (sc)
        for (const EntityPtr& child : sc->children)
            child->updateTree(renderer, world, deltaSeconds, editorPaused);
}

EntityPtr Entity::create(const EntitySpawnTemplate& tmpl, const Transform& transform, uint8 initialFlags)
{
    void* buffer = Globals::entityAllocator.allocate(tmpl.archetype.allocSize);

    Entity* entity = ::new (buffer) Entity();
    entity->pos = transform.pos;
    entity->scale = transform.scale;
    entity->rot = transform.quat;

    entity->displayName = tmpl.displayName;
    entity->spawnTemplate = &tmpl;
    entity->typeBits = tmpl.archetype.typeBits;
    entity->flags = initialFlags;
    entity->setPrefabInstance(!tmpl.prefabName.empty()); // a registered prefab spawns as a locked instance

    int idx = 0;
    uint16 offset = EntityComponentDetail::entityBaseOffset;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
        {
            entity->createComponent(EComponentID(i), offset, tmpl.spawnInfos[idx++].get(), transform);
            offset += EntityComponentDetail::inlineSizes[i];
        }

    return EntityPtr(entity);
}

void Entity::destroy(Entity* entity)
{
    const uint32 size = getEntityAllocSize(entity->typeBits);

    int idx = 0;
    uint16 offset = EntityComponentDetail::entityBaseOffset;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
        {
            entity->destroyComponent(EComponentID(i), offset, entity->spawnTemplate->spawnInfos[idx++].get());
            offset += EntityComponentDetail::inlineSizes[i];
        }

    entity->~Entity();
    Globals::entityAllocator.deallocate(entity, size);
}

void Entity::createComponent(EComponentID id, uint16 componentOffset, const void* info, const Transform& base)
{
    switch (id)
    {
    case EComponentID_Scene:
    {
        SceneComponent* sc = reinterpret_cast<SceneComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        new (sc) SceneComponent();
        sc->spawn(*this, *static_cast<const SceneComponent::SpawnInfo*>(info), base);
        break;
    }
    case EComponentID_Render:
    {
        RenderComponent* rc = reinterpret_cast<RenderComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        new (rc) RenderComponent();
        rc->spawn(*this, *static_cast<const RenderComponent::SpawnInfo*>(info), base);
        break;
    }
    case EComponentID_Animator:
    {
        AnimatorComponent* ac = reinterpret_cast<AnimatorComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        new (ac) AnimatorComponent();
        ac->spawn(*this, *static_cast<const AnimatorComponent::SpawnInfo*>(info), base);
        break;
    }
    case EComponentID_Physics:
    {
        PhysicsComponent* pc = reinterpret_cast<PhysicsComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        new (pc) PhysicsComponent();
        pc->spawn(*this, *static_cast<const PhysicsComponent::SpawnInfo*>(info), base);
        break;
    }
    case EComponentID_Audio:
    {
        AudioComponent* ac = reinterpret_cast<AudioComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        new (ac) AudioComponent();
        ac->spawn(*this, *static_cast<const AudioComponent::SpawnInfo*>(info), base);
        break;
    }
    case EComponentID_Script:
    {
        ScriptComponent* scr = reinterpret_cast<ScriptComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        new (scr) ScriptComponent();
        scr->spawn(*this, *static_cast<const ScriptComponent::SpawnInfo*>(info), base);
        break;
    }
    default:
        __debugbreak();
    }
}

void Entity::destroyComponent(EComponentID id, uint16 componentOffset, const void* info)
{
    switch (id)
    {
    case EComponentID_Scene:
    {
        SceneComponent* sc = reinterpret_cast<SceneComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        sc->destroy(*this, *static_cast<const SceneComponent::SpawnInfo*>(info));
        sc->~SceneComponent();
        break;
    }
    case EComponentID_Render:
    {
        RenderComponent* rc = reinterpret_cast<RenderComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        rc->destroy(*this, *static_cast<const RenderComponent::SpawnInfo*>(info));
        rc->~RenderComponent();
        break;
    }
    case EComponentID_Animator:
    {
        AnimatorComponent* ac = reinterpret_cast<AnimatorComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        ac->destroy(*this, *static_cast<const AnimatorComponent::SpawnInfo*>(info));
        ac->~AnimatorComponent();
        break;
    }
    case EComponentID_Physics:
    {
        PhysicsComponent* pc = reinterpret_cast<PhysicsComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        pc->destroy(*this, *static_cast<const PhysicsComponent::SpawnInfo*>(info));
        pc->~PhysicsComponent();
        break;
    }
    case EComponentID_Audio:
    {
        AudioComponent* ac = reinterpret_cast<AudioComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        ac->destroy(*this, *static_cast<const AudioComponent::SpawnInfo*>(info));
        ac->~AudioComponent();
        break;
    }
    case EComponentID_Script:
    {
        ScriptComponent* scr = reinterpret_cast<ScriptComponent*>(reinterpret_cast<uint8*>(this) + componentOffset);
        scr->destroy(*this, *static_cast<const ScriptComponent::SpawnInfo*>(info));
        scr->~ScriptComponent();
        break;
    }
    default:
        __debugbreak();
    }
}

void Entity::serializeComponent(EComponentID id, AssetNode& out)
{
    switch (id)
    {
    case EComponentID_Scene:  getComponent<SceneComponent>(this)->serialize(out);   break;
    case EComponentID_Zone:   getComponent<ZoneComponent>(this)->serialize(out);     break;
    case EComponentID_Cull:   getComponent<CullingComponent>(this)->serialize(out);  break;
    case EComponentID_Render: getComponent<RenderComponent>(this)->serialize(out);   break;
    case EComponentID_Animator: getComponent<AnimatorComponent>(this)->serialize(out); break;
    case EComponentID_Physics: getComponent<PhysicsComponent>(this)->serialize(out);  break;
    case EComponentID_Audio:  getComponent<AudioComponent>(this)->serialize(out);    break;
    case EComponentID_Script: getComponent<ScriptComponent>(this)->serialize(out);   break;
    default: break;
    }
}

void Entity::deserializeComponent(EComponentID id, const AssetNode& in)
{
    switch (id)
    {
    case EComponentID_Scene:  getComponent<SceneComponent>(this)->deserialize(in);   break;
    case EComponentID_Zone:   getComponent<ZoneComponent>(this)->deserialize(in);     break;
    case EComponentID_Cull:   getComponent<CullingComponent>(this)->deserialize(in);  break;
    case EComponentID_Render: getComponent<RenderComponent>(this)->deserialize(in);   break;
    case EComponentID_Animator: getComponent<AnimatorComponent>(this)->deserialize(in); break;
    case EComponentID_Physics: getComponent<PhysicsComponent>(this)->deserialize(in);  break;
    case EComponentID_Audio:  getComponent<AudioComponent>(this)->deserialize(in);    break;
    case EComponentID_Script: getComponent<ScriptComponent>(this)->deserialize(in);   break;
    default: break;
    }
}

static bool isSelfOrDescendant(Entity* node, Entity* ancestor)
{
    for (Entity* p = node; p; p = p->parent)
        if (p == ancestor)
            return true;
    return false;
}

void Entity::reparentEntity(Entity* newParent)
{
    if (this == newParent || parent == newParent)
        return;

    EntityPtr keepAlive(this);
    detachFromOwner(this);

    if (newParent && !hasComponent<SceneComponent>(newParent))
        return; // only scene entities can hold children
    if (newParent && isSelfOrDescendant(newParent, this))
        return; // would create a cycle

    parent = newParent;

    if (newParent)
        getComponent<SceneComponent>(newParent)->children.emplace_back(std::move(keepAlive));
}

Entity* Entity::nearestPrefabInstance()
{
    for (Entity* p = this; p; p = p->parent)
        if (p->isPrefabInstance())
            return p;
    return nullptr;
}

bool Entity::isPrefabLocked() const
{
    for (const Entity* p = this; p; p = p->parent)
        if (p->isPrefabInstance())
            return true;
    return false;
}

const std::string& Entity::getPrefabName() const
{
    static const std::string empty;
    return spawnTemplate ? spawnTemplate->prefabName : empty;
}

const std::string& Entity::getSourceFile() const
{
    static const std::string empty;
    return spawnTemplate ? spawnTemplate->sourceFile : empty;
}
