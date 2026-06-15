export module Entity.Component;

import Entity;
import Core;
import Core.glm;

import RendererVK;

// Inline component ids. Components with id < MaxInlineComponentTypes are stored inline, packed in id
// order in the bytes that follow the Entity header. (Higher ids are reserved for future out-of-line
// "ECS" components referenced by handle.)
export enum EComponentID : uint16
{
    EComponentID_Zone   = 0,
    EComponentID_Cull   = 1,
    EComponentID_Render = 2,

    EComponentID_GameLogic = 3,
};

export constexpr uint16 MaxInlineComponentTypes = 3;

// Inline components are packed on this boundary so each one is suitably aligned regardless of which
// preceding components are present. 16 covers every inline component type here.
export constexpr uint16 ComponentAlignment = 16;

// ---- inline component types -------------------------------------------------

export struct ZoneComponent
{
    static constexpr EComponentID getId() { return EComponentID_Zone; }
    std::vector<EntityPtr> entities;
};

export struct CullingComponent
{
    static constexpr EComponentID getId() { return EComponentID_Cull; }
    float radius = 0.0f;
    glm::vec3 center = glm::vec3(0.0f);
    glm::vec3 extent = glm::vec3(0.0f);
};

export struct RenderComponent
{
    static constexpr EComponentID getId() { return EComponentID_Render; }
    RenderNode node;
};

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
        alignUp(uint16(sizeof(ZoneComponent)),    ComponentAlignment),
        alignUp(uint16(sizeof(CullingComponent)), ComponentAlignment),
        alignUp(uint16(sizeof(RenderComponent)),  ComponentAlignment),
    };
    inline constexpr std::array<ConstructFn, MaxInlineComponentTypes> inlineConstructors {
        &construct<ZoneComponent>, &construct<CullingComponent>, &construct<RenderComponent> };
    inline constexpr std::array<DestructFn, MaxInlineComponentTypes> inlineDestructors {
        &destruct<ZoneComponent>, &destruct<CullingComponent>, &destruct<RenderComponent> };

    // Components start past the (aligned) Entity header.
    inline constexpr uint16 entityBaseOffset = alignUp(uint16(sizeof(Entity)), ComponentAlignment);
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
