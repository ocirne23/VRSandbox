module Entity;

import Core;
import Core.Transform;
import File.AssetParser;
import Entity.Component;
import Entity.Allocator;

EntityArchetype makeEntityArchetype(uint16 typeBits)
{
    return EntityArchetype{ uint16(getEntityAllocSize(typeBits)), typeBits };
}

EntityPtr Entity::create(const EntitySpawnTemplate& tmpl, const Transform& transform)
{
    void* buffer = Globals::entityAllocator.allocate(tmpl.archetype.allocSize);

    Entity* entity = ::new (buffer) Entity();
    entity->pos = transform.pos;
    entity->scale = transform.scale;
    entity->rot = transform.quat;

    entity->name = tmpl.displayName;
    entity->spawnTemplate = &tmpl;
    entity->typeBits = tmpl.archetype.typeBits;
    entity->setPrefabInstance(!tmpl.prefabName.empty()); // a registered prefab spawns as a locked instance

    int idx = 0;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            entity->createComponent(EComponentID(i), tmpl.spawnInfos[idx++].get(), transform);

    return EntityPtr(entity);
}

void Entity::destroy(Entity* entity)
{
    const uint32 size = getEntityAllocSize(entity->typeBits);

    int idx = 0;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            entity->destroyComponent(EComponentID(i), entity->spawnTemplate->spawnInfos[idx++].get());

    entity->~Entity();
    Globals::entityAllocator.deallocate(entity, size);
}

void Entity::createComponent(EComponentID id, const void* info, const Transform& base)
{
    switch (id)
    {
    case EComponentID_Scene:
    {
        SceneComponent* sc = getComponent<SceneComponent>(this);
        new (sc) SceneComponent();
        sc->spawn(*this, *static_cast<const SceneComponent::SpawnInfo*>(info), base);
        break;
    }
    case EComponentID_Render:
    {
        RenderComponent* rc = getComponent<RenderComponent>(this);
        new (rc) RenderComponent();
        rc->spawn(*this, *static_cast<const RenderComponent::SpawnInfo*>(info), base);
        break;
    }
    default:
        __debugbreak();
    }
}

void Entity::destroyComponent(EComponentID id, const void* info)
{
    switch (id)
    {
    case EComponentID_Scene:
    {
        SceneComponent* sc = getComponent<SceneComponent>(this);
        sc->destroy(*this, *static_cast<const SceneComponent::SpawnInfo*>(info));
        sc->~SceneComponent();
        break;
    }
    case EComponentID_Render:
    {
        RenderComponent* rc = getComponent<RenderComponent>(this);
        rc->destroy(*this, *static_cast<const RenderComponent::SpawnInfo*>(info));
        rc->~RenderComponent();
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
    if (newParent && !hasComponent<SceneComponent>(newParent))
        return; // only scene entities can hold children
    if (newParent && isSelfOrDescendant(newParent, this))
        return; // would create a cycle

    EntityPtr keepAlive(this);

    detachFromOwner(this);
    parent = newParent;

    if (newParent)
        getComponent<SceneComponent>(newParent)->children.emplace_back(this);
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