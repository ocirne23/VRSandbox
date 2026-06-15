export module Entity;

import Core;
import Core.glm;
import Core.Transform;

// Lightweight entity header. Its components live in memory immediately after this header; which
// components are present is encoded in typeBits, and each component's byte offset is derived from
// that mask (see Entity.Component). Entities are created via createEntity() (which allocates the
// header + trailing component bytes from the EntityAllocator), never constructed standalone.
export struct Entity
{
    glm::vec3 pos;
    float scale = 1.0f;
    glm::quat rot;

    uint16 refCount = 0;
    uint16 typeBits = 0;
    uint8 ecsComponentCount = 0;
    uint8 zoneIdx = 0;

    ~Entity()
    {
        assert(refCount == 0);
    }
};

// Tears an entity down and returns its memory: runs the inline component destructors, then ~Entity,
// then deallocates the contiguous block. Defined in Entity.cpp, where the allocator and the inline
// component tables are visible. Only EntityPtr (on the last reference) should call this.
export void destroyEntity(Entity* entity);

// Intrusive refcounted handle to an entity. The count lives in Entity::refCount; when it falls to
// zero the entity is freed via destroyEntity(). Copy = share, move = transfer. Default-constructed
// (or moved-from) handles are null and safe to destroy. Copyable/movable so it can live in
// containers (e.g. ZoneComponent::entities).
export struct EntityPtr
{
    EntityPtr() = default;
    explicit EntityPtr(Entity* entity) : m_entity(entity) { addRef(); }

    EntityPtr(const EntityPtr& other) : m_entity(other.m_entity) { addRef(); }
    EntityPtr(EntityPtr&& other) noexcept : m_entity(other.m_entity) { other.m_entity = nullptr; }

    EntityPtr& operator=(const EntityPtr& other)
    {
        if (this != &other)
        {
            release();
            m_entity = other.m_entity;
            addRef();
        }
        return *this;
    }

    EntityPtr& operator=(EntityPtr&& other) noexcept
    {
        if (this != &other)
        {
            release();
            m_entity = other.m_entity;
            other.m_entity = nullptr;
        }
        return *this;
    }

    ~EntityPtr() { release(); }

    Entity* get() const { return m_entity; }
    Entity* operator->() const { return m_entity; }
    Entity& operator*() const { return *m_entity; }
    explicit operator bool() const { return m_entity != nullptr; }

    // Implicit decay to the raw non-owning pointer, so an EntityPtr can be passed anywhere an
    // Entity* is expected (e.g. getComponent<T>(entity)) without calling get(). Non-owning — the
    // returned pointer must not outlive the handle.
    operator Entity*() const { return m_entity; }

    // Drop this handle's reference early (frees the entity if it was the last one) and become null.
    // Safe to call on an already-null handle and to call repeatedly.
    void release()
    {
        if (!m_entity)
            return;
        if (std::atomic_ref<uint16>(m_entity->refCount).fetch_sub(1) == 1)
            destroyEntity(m_entity);
        m_entity = nullptr;
    }

private:

    void addRef()
    {
        if (m_entity)
            std::atomic_ref<uint16>(m_entity->refCount).fetch_add(1);
    }

    Entity* m_entity = nullptr;
};

// An entity "archetype": the component mask plus the allocation size it implies. Computed once (the
// size is derived from the mask via getEntityAllocSize) and cached by callers that spawn the same
// kind repeatedly, so each spawn skips recomputing the size and re-interpreting the source asset.
export struct EntityArchetype
{
    uint16 allocSize = 0;
    uint16 typeBits = 0;
};

// Builds the archetype for a component mask, computing its allocation size. Defined in Entity.cpp.
export EntityArchetype makeEntityArchetype(uint16 typeBits);

// Allocates and constructs an entity for the given archetype, initialised to the given transform,
// and returns the first owning handle to it. The archetype form is the fast path (no size recompute);
// the typeBits form is a convenience that builds the archetype first. Both defined in Entity.cpp.
export EntityPtr createEntity(const EntityArchetype& archetype, const Transform& transform);
export EntityPtr createEntity(uint16 typeBits, const Transform& transform);
