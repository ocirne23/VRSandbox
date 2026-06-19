export module Entity:Component;

import :Entity;
import Core;
import Core.glm;
import Core.Transform;
import File;

import RendererVK;

export int componentIdFromName(std::string_view name);
export void detachFromOwner(Entity* entity);

export constexpr uint16 MaxInlineComponentTypes = 4;
export constexpr uint16 ComponentAlignment = 16;

export struct SceneComponent
{
    static constexpr EComponentID getId() { return EComponentID_Scene; }

    Entity* getEntity();

    struct SpawnInfo
    {
        struct ChildSpawnInfo
        {
            std::shared_ptr<const EntitySpawnTemplate> tmpl;
            Transform localTransform;                  // placement composed onto the parent's spawn transform
            std::string name;                          // name override, empty = use the template's
        };
        std::vector<ChildSpawnInfo> children;
        bool enabled = true;
    };

    std::vector<EntityPtr> children;
    bool enabled = true;

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
	void destroy(Entity& entity, const SpawnInfo& info);

    void serialize(AssetNode& out) const { out.set("Enabled", enabled); }
    void deserialize(const AssetNode& in)
    {
        if (const AssetNode* n = in.find("Enabled")) enabled = n->asBool();
    }
};

export struct ZoneComponent
{
    static constexpr EComponentID getId() { return EComponentID_Zone; }
    std::vector<EntityPtr> entities;

    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}
};

export struct CullingComponent
{
    static constexpr EComponentID getId() { return EComponentID_Cull; }
    float radius = 0.0f;
    glm::vec3 center = glm::vec3(0.0f);
    glm::vec3 extent = glm::vec3(0.0f);

    void serialize(AssetNode& out) const
    {
        out.set("Radius", radius);
        out.set("Center", center);
        out.set("Extent", extent);
    }
    void deserialize(const AssetNode& in)
    {
        if (const AssetNode* n = in.find("Radius")) radius = n->asFloat();
        if (const AssetNode* n = in.find("Center")) center = n->asVec3();
        if (const AssetNode* n = in.find("Extent")) extent = n->asVec3();
    }
};

export struct RenderComponent
{
    static constexpr EComponentID getId() { return EComponentID_Render; }

    ~RenderComponent();

    RenderNode node;
    Transform localTransform;
    bool showBounds = false;

    struct SpawnInfo
    {
        ObjectContainer* container = nullptr;       // null = nothing to spawn
        std::string containerName;                  // ObjectContainer reference name, kept for re-serialization
        std::string nodePath;                       // For debug/display. nodeIdx is used at runtime for spawning.
        NodeSpawnIdx nodeIdx = NodeSpawnIdx_ROOT;
        Transform localTransform;                   // applied on top of the spawn base transform
    };

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);

    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}
};

export Transform composeTransform(const Transform& parent, const Transform& local);

export const RenderComponent::SpawnInfo* getRenderSpawnInfo(const Entity* entity);

export constexpr const char* componentTypeName(EComponentID id)
{
    switch (id)
    {
    case EComponentID_Scene:  return "Scene";
    case EComponentID_Zone:   return "Zone";
    case EComponentID_Cull:   return "Cull";
    case EComponentID_Render: return "RenderNode";
    default:                  return "Unknown";
    }
}

export namespace EntityComponentDetail
{
    template <typename T>
    constexpr T alignUp(T value, T alignment) { return (value + alignment - 1) & ~(alignment - 1); }
    inline constexpr std::array<uint16, MaxInlineComponentTypes> inlineSizes {
        alignUp(uint16(sizeof(SceneComponent)),   ComponentAlignment),
        alignUp(uint16(sizeof(ZoneComponent)),    ComponentAlignment),
        alignUp(uint16(sizeof(CullingComponent)), ComponentAlignment),
        alignUp(uint16(sizeof(RenderComponent)),  ComponentAlignment),
    };

    inline constexpr uint16 entityBaseOffset = alignUp(uint16(sizeof(Entity)), ComponentAlignment);
}

inline Entity* SceneComponent::getEntity()
{
    return reinterpret_cast<Entity*>(reinterpret_cast<uint8*>(this) - EntityComponentDetail::entityBaseOffset);
}

export constexpr uint16 getComponentByteOffset(uint16 typeBits, EComponentID id)
{
    uint16 offset = EntityComponentDetail::entityBaseOffset;
    for (uint16 i = 0; i < uint16(id) && i < MaxInlineComponentTypes; ++i)
        if (typeBits & (1 << i))
            offset += EntityComponentDetail::inlineSizes[i];
    return offset;
}

export constexpr uint16 getEntityAllocSize(uint16 typeBits)
{
    uint16 size = EntityComponentDetail::entityBaseOffset;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (typeBits & (1 << i))
            size += EntityComponentDetail::inlineSizes[i];
    return size;
}

export template <typename T>
bool hasComponent(const Entity* entity)
{
    return (entity->typeBits & (1 << T::getId())) != 0;
}

export template <typename T>
T* getComponent(Entity* entity)
{
    constexpr EComponentID id = T::getId();
    static_assert(uint16(id) < MaxInlineComponentTypes, "Only inline components are supported");
    if (!(entity->typeBits & (1 << id)))
        return nullptr;
    return reinterpret_cast<T*>(reinterpret_cast<uint8*>(entity) + getComponentByteOffset(entity->typeBits, id));
}
