export module Entity.Component;

import Entity;
import Core;
import Core.glm;
import Core.Transform;
import File.AssetParser;

import RendererVK;

// Inline component ids. Components with id < MaxInlineComponentTypes are stored inline, packed in id
// order in the bytes that follow the Entity header. (Higher ids are reserved for future out-of-line
// "ECS" components referenced by handle.)
export enum EComponentID : uint16
{
    EComponentID_Scene  = 0,
    EComponentID_Zone   = 1,
    EComponentID_Cull   = 2,
    EComponentID_Render = 3,

    EComponentID_GameLogic = 4,
};

export constexpr uint16 MaxInlineComponentTypes = 4;

// Inline components are packed on this boundary so each one is suitably aligned regardless of which
// preceding components are present. 16 covers every inline component type here.
export constexpr uint16 ComponentAlignment = 16;

// ---- inline component types -------------------------------------------------

// Presence of a SceneComponent is what lets an entity hold children (loose entities can be parented
// but are always leaves). The owning `children` handles drive lifetime down the tree; the matching
// `Entity::parent` back-pointer is non-owning, so there is no refcount cycle. Root scene entities
// (parent == nullptr) are owned by the EntityRegistry's scene-root list.
// Each inline component exposes serialize()/deserialize() over the shared AssetNode text format,
// driven by the prefab (.pre) serializer. They cover only the component's own intrinsic data: the
// scene-graph structure (which entity is whose child) and the heavy RenderNode are reconstructed by
// the prefab loader from Entity::sourceAsset, not from per-component data.

export struct SceneComponent
{
    static constexpr EComponentID getId() { return EComponentID_Scene; }

    Entity* getEntity();

    std::vector<EntityPtr> children;
    bool enabled = true;

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

    // Runtime spatial membership — nothing intrinsic to persist.
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
    RenderNode node;

    // The mesh/RenderNode is restored by the prefab loader via Entity::sourceAsset, so there is no
    // intrinsic render data to write here.
    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}
};

// Stable text type-name for each inline component, used as the "Component <name>" key in prefabs.
export constexpr const char* componentTypeName(EComponentID id)
{
    switch (id)
    {
    case EComponentID_Scene:  return "Scene";
    case EComponentID_Zone:   return "Zone";
    case EComponentID_Cull:   return "Cull";
    case EComponentID_Render: return "Render";
    default:                  return "Unknown";
    }
}

// Inverse of componentTypeName; returns the matching EComponentID, or -1 if the name is unknown.
export int componentIdFromName(std::string_view name);

// Dispatch (de)serialization of the inline component `id` on `entity` (which must currently have it).
export void serializeComponent(Entity* entity, EComponentID id, AssetNode& out);
export void deserializeComponent(Entity* entity, EComponentID id, const AssetNode& in);

// ---- packing internals ------------------------------------------------------

namespace EntityComponentDetail
{
    template <typename T>
    constexpr T alignUp(T value, T alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    template <typename T> void construct(void* p) { ::new (p) T(); }
    template <typename T> void destruct(void* p) { static_cast<T*>(p)->~T(); }

    using ConstructFn = void (*)(void*);
    using DestructFn  = void (*)(void*);

    inline constexpr std::array<uint16, MaxInlineComponentTypes> inlineSizes {
        alignUp(uint16(sizeof(SceneComponent)),   ComponentAlignment),
        alignUp(uint16(sizeof(ZoneComponent)),    ComponentAlignment),
        alignUp(uint16(sizeof(CullingComponent)), ComponentAlignment),
        alignUp(uint16(sizeof(RenderComponent)),  ComponentAlignment),
    };
    inline constexpr std::array<ConstructFn, MaxInlineComponentTypes> inlineConstructors {
        &construct<SceneComponent>, &construct<ZoneComponent>, &construct<CullingComponent>, &construct<RenderComponent> };
    inline constexpr std::array<DestructFn, MaxInlineComponentTypes> inlineDestructors {
        &destruct<SceneComponent>, &destruct<ZoneComponent>, &destruct<CullingComponent>, &destruct<RenderComponent> };

    // Components start past the (aligned) Entity header.
    inline constexpr uint16 entityBaseOffset = alignUp(uint16(sizeof(Entity)), ComponentAlignment);
}

inline Entity* SceneComponent::getEntity()
{
    return reinterpret_cast<Entity*>(reinterpret_cast<uint8*>(this) - EntityComponentDetail::entityBaseOffset);
}

// ---- public component API ---------------------------------------------------

// Byte offset of inline component `id` from the start of the entity, given its component mask.
export constexpr uint16 getComponentByteOffset(uint16 typeBits, EComponentID id)
{
    uint16 offset = EntityComponentDetail::entityBaseOffset;
    for (uint16 i = 0; i < uint16(id) && i < MaxInlineComponentTypes; ++i)
        if (typeBits & (1 << i))
            offset += EntityComponentDetail::inlineSizes[i];
    return offset;
}

// Total block size for an entity carrying the components in `typeBits`.
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

// Returns the inline component T on entity, or nullptr if the entity doesn't have it.
export template <typename T>
T* getComponent(Entity* entity)
{
    constexpr EComponentID id = T::getId();
    static_assert(uint16(id) < MaxInlineComponentTypes, "Only inline components are supported");
    if (!(entity->typeBits & (1 << id)))
        return nullptr;
    return reinterpret_cast<T*>(reinterpret_cast<uint8*>(entity) + getComponentByteOffset(entity->typeBits, id));
}

// Run the lifetimes of an entity's inline components. Called by EntityManager around (de)allocation.
export void constructInlineComponents(Entity* entity);
export void destructInlineComponents(Entity* entity);

// Spawns an entity with the given components plus a SceneComponent, sets its name, and registers it
// as a root in the EntityRegistry (so it shows up under "World" and stays alive even if the caller
// drops the returned handle). Convenience over createEntity for scene-graph entities.
export EntityPtr createSceneEntity(uint16 typeBits, const Transform& transform, const char* name = nullptr);

// Reparents any entity under `newParent`, transferring its owning handle between the old owner (its
// previous parent's children list, the registry's scene roots, or — for an externally-owned loose
// root — nothing) and the new one. `newParent == nullptr` unparents it back to the top level (a
// SceneComponent entity becomes a registry-owned "World" root; a loose entity reverts to external
// ownership). `newParent`, if given, must be a scene entity (only those can hold children). No-ops if
// the move is a no-op or would create a cycle (newParent is `child` or a descendant of it).
export void reparentEntity(Entity* child, Entity* newParent);

// Detaches an entity from its scene-graph owner (its parent's children list, or the registry's scene
// roots), dropping that owning reference. If nothing else holds the entity this destroys it and,
// recursively, the subtree it owns.
export void removeEntity(Entity* entity);
