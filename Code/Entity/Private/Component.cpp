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

void RenderComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    if (!info.container)
        return;
    localTransform = info.localTransform;
    node = info.container->spawnNodeForIdx(info.nodeIdx, composeTransform(base, info.localTransform));
}

void RenderComponent::destroy(Entity& entity, const SpawnInfo& info)
{

}

RenderComponent::~RenderComponent()
{
    __debugbreak();
}

void SceneComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    enabled = info.enabled;
    for (const SpawnInfo::ChildSpawnInfo& child : info.children)
    {
        if (!child.tmpl)
            continue;
        EntityPtr childEntity = createEntity(*child.tmpl, child.localTransform);
        if (!child.name.empty())
            childEntity->name = child.name;
        reparentEntity(childEntity, &entity); // hands the owning child handle to this entity's children list
    }
}

void SceneComponent::destroy(Entity& entity, const SpawnInfo& info)
{

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

void createComponent(Entity* entity, EComponentID id, const void* info, const Transform& base)
{
    switch (id)
    {
    case EComponentID_Scene:
    {
        SceneComponent* sc = getComponent<SceneComponent>(entity);
        new (sc) SceneComponent();
        sc->spawn(*entity, *static_cast<const SceneComponent::SpawnInfo*>(info), base);
        break;
    }
    case EComponentID_Render:
    {
        RenderComponent* rc = getComponent<RenderComponent>(entity);
        new (rc) RenderComponent();
        rc->spawn(*entity, *static_cast<const RenderComponent::SpawnInfo*>(info), base);
        break;
    }
    default: 
        __debugbreak();
    }
}

void destroyComponent(Entity* entity, EComponentID id, const void* info)
{
    switch (id)
    {
    case EComponentID_Scene:
    {
        SceneComponent* sc = getComponent<SceneComponent>(entity);
        sc->destroy(*entity, *static_cast<const SceneComponent::SpawnInfo*>(info));
        sc->~SceneComponent();
        break;
    }
    case EComponentID_Render:
    {
        RenderComponent* rc = getComponent<RenderComponent>(entity);
        rc->destroy(*entity, *static_cast<const RenderComponent::SpawnInfo*>(info));
        rc->~RenderComponent();
        break;
    }
    default: 
        __debugbreak();
    }
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