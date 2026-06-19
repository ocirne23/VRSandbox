module Entity;

import Core;
import Core.Transform;
import File;
import :Component;
import :Allocator;

import RendererVK;

EntityArchetype makeEntityArchetype(uint16 typeBits)
{
    return EntityArchetype{ uint16(getEntityAllocSize(typeBits)), typeBits };
}

void Entity::renderTree(Renderer& renderer, const Transform& parentWorld)
{
    SceneComponent* sc = getComponent<SceneComponent>(this);
    if (sc && !sc->enabled)
        return;

    const Transform world = composeTransform(parentWorld, Transform(pos, scale, rot));

    if (RenderComponent* render = getComponent<RenderComponent>(this))
    {
        render->node.getTransform() = composeTransform(world, render->localTransform);
        renderer.renderNode(render->node);
    }

    if (sc)
        for (const EntityPtr& child : sc->children)
            child->renderTree(renderer, world);
}

EntityPtr Entity::create(const EntitySpawnTemplate& tmpl, const Transform& transform)
{
    void* buffer = Globals::entityAllocator.allocate(tmpl.archetype.allocSize);

    Entity* entity = ::new (buffer) Entity();
    entity->pos = transform.pos;
    entity->scale = transform.scale;
    entity->rot = transform.quat;

    entity->displayName = tmpl.displayName;
    entity->spawnTemplate = &tmpl;
    entity->typeBits = tmpl.archetype.typeBits;
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
