module Entity.Component;

import Core;
import Core.glm;
import Core.Transform;
import Entity;
import RendererVK;

Transform composeTransform(const Transform& parent, const Transform& local)
{
    return Transform(
        parent.pos + parent.quat * (local.pos * parent.scale),
        parent.scale * local.scale,
        glm::normalize(parent.quat * local.quat));
}

void RenderComponent::spawn(const SpawnInfo& info, const Transform& base)
{
    if (!info.container)
        return;
    localTransform = info.localTransform;
    node = info.container->spawnNodeForIdx(info.nodeIdx, composeTransform(base, info.localTransform));
}

void SceneComponent::spawn(const SpawnInfo& info, const Transform& base)
{
    enabled = info.enabled;
    Entity* self = getEntity();
    for (const SpawnInfo::ChildSpawnInfo& child : info.children)
    {
        if (!child.tmpl)
            continue;
        EntityPtr childEntity = spawnFromTemplate(*child.tmpl, child.localTransform);
        if (!child.name.empty())
            childEntity->name = child.name;
        reparentEntity(childEntity, self); // hands the owning child handle to this entity's children list
    }
}

EntityPtr spawnFromTemplate(const EntitySpawnTemplate& tmpl, const Transform& base)
{
    EntityPtr entity = createEntity(tmpl.archetype, base);
    entity->spawnTemplate = &tmpl;
    entity->name = tmpl.displayName;

    size_t idx = 0;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
    {
        if (!(tmpl.archetype.typeBits & (1 << i)))
            continue;
        if (const void* info = tmpl.spawnInfos[idx].get())
            spawnComponent(entity, EComponentID(i), info, base);
        ++idx;
    }
    return entity;
}

EntityPtr createSceneEntity(uint16 typeBits, const Transform& transform, const char* name)
{
    EntityPtr entity = createEntity(typeBits | (1 << EComponentID_Scene), transform);
    if (name)
        entity->name = name;
    return entity;
}

static bool isSelfOrDescendant(Entity* node, Entity* ancestor)
{
    for (Entity* p = node; p; p = p->parent)
        if (p == ancestor)
            return true;
    return false;
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

static void detachFromOwner(Entity* child)
{
    if (child->parent)
        detachFromParent(child->parent, child);
}

void reparentEntity(Entity* child, Entity* newParent)
{
    if (!child)
        return;
    if (child == newParent || child->parent == newParent)
        return;
    if (newParent && !hasComponent<SceneComponent>(newParent))
        return; // only scene entities can hold children
    if (newParent && isSelfOrDescendant(newParent, child))
        return; // would create a cycle

    EntityPtr keepAlive(child);

    detachFromOwner(child);
    child->parent = newParent;

    if (newParent)
        getComponent<SceneComponent>(newParent)->children.emplace_back(child);
}

void removeEntity(Entity* entity)
{
    if (entity)
        detachFromOwner(entity);
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

int componentIdFromName(std::string_view name)
{
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (name == componentTypeName(EComponentID(i)))
            return int(i);
    return -1;
}

void serializeComponent(Entity* entity, EComponentID id, AssetNode& out)
{
    switch (id)
    {
    case EComponentID_Scene:  getComponent<SceneComponent>(entity)->serialize(out);   break;
    case EComponentID_Zone:   getComponent<ZoneComponent>(entity)->serialize(out);     break;
    case EComponentID_Cull:   getComponent<CullingComponent>(entity)->serialize(out);  break;
    case EComponentID_Render: getComponent<RenderComponent>(entity)->serialize(out);   break;
    default: break;
    }
}

void deserializeComponent(Entity* entity, EComponentID id, const AssetNode& in)
{
    switch (id)
    {
    case EComponentID_Scene:  getComponent<SceneComponent>(entity)->deserialize(in);   break;
    case EComponentID_Zone:   getComponent<ZoneComponent>(entity)->deserialize(in);     break;
    case EComponentID_Cull:   getComponent<CullingComponent>(entity)->deserialize(in);  break;
    case EComponentID_Render: getComponent<RenderComponent>(entity)->deserialize(in);   break;
    default: break;
    }
}

void spawnComponent(Entity* entity, EComponentID id, const void* info, const Transform& base)
{
    switch (id)
    {
    case EComponentID_Scene:
        getComponent<SceneComponent>(entity)->spawn(*static_cast<const SceneComponent::SpawnInfo*>(info), base);
        break;
    case EComponentID_Render:
        getComponent<RenderComponent>(entity)->spawn(*static_cast<const RenderComponent::SpawnInfo*>(info), base);
        break;
    default: break; // components without a spawn step
    }
}

void constructInlineComponents(Entity* entity)
{
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            EntityComponentDetail::inlineConstructors[i](
                reinterpret_cast<uint8*>(entity) + getComponentByteOffset(entity->typeBits, EComponentID(i)));
}

void destructInlineComponents(Entity* entity)
{
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            EntityComponentDetail::inlineDestructors[i](
                reinterpret_cast<uint8*>(entity) + getComponentByteOffset(entity->typeBits, EComponentID(i)));
}
