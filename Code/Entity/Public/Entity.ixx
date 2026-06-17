export module Entity;

import Core;
import Core.glm;
import Core.Transform;

export struct EntitySpawnTemplate;

// Lightweight entity header. Its components live in memory immediately after this header; which
// components are present is encoded in typeBits, and each component's byte offset is derived from
// that mask (see Entity.Component). Entities are created via createEntity() (which allocates the
// header + trailing component bytes from the EntityAllocator), never constructed standalone.
export struct Entity
{
    glm::vec3 pos;
    float scale = 1.0f;
    glm::quat rot;

    // Display name and scene-graph back-pointer live on every entity (not just SceneComponent ones):
    // any entity can be parented under a scene entity, but only entities with a SceneComponent can
    // hold children. `parent` is a non-owning back-pointer (null = root); ownership flows DOWN through
    // SceneComponent::children. See Entity.Component for the reparent/registry ownership rules.
    Entity* parent = nullptr;
    std::string name;

    // Opaque pointer to the Scene::SpawnTemplate this entity was spawned from (set by
    // Scene::World::spawn), or null for editor-authored entities. Stored as void* because the Scene
    // layer sits above Entity and can't be named here; Scene owns the templates (heap-allocated, so the
    // address is stable) and casts this back to its concrete type.
    const EntitySpawnTemplate* spawnTemplate = nullptr;

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

// Everything needed to spawn a named object without touching its asset desc again: the
// entity archetype (alloc size + component mask) plus each spawnable component's cached
// SpawnInfo. Built once per name and reused on every spawn.
export struct EntitySpawnTemplate
{
    EntityArchetype archetype;                     // alloc size + component mask, computed once
    Transform defaultTransform;                    // baked from the source asset's authored Position/Rotation/Scale
    // One slot per set bit in archetype.typeBits, in component-id order: a type-erased
    // <Component>::SpawnInfo whose concrete type is implied by the bit it lines up with
    // (cast via spawnComponent). Null where a present component has no spawn step.
    std::vector<std::shared_ptr<void>> spawnInfos;

    // Path of the ".pre" file this template was loaded from, or empty for an inline-entity template
    // (which lives inside its parent's file rather than its own).
    std::string sourceFile;

    // Registered prefab name this template is the root of, or empty for an inline-entity template.
    // Prefab serialization reads it (via Entity::spawnTemplate) to decide whether a child re-serializes
    // as a "Prefab <name>" reference or an inline "Entity" with its full body.
    std::string prefabName;

    // Default display name given to spawned entities (the asset's authored "Name", falling back to the
    // declaration token). Per-instance overrides still win — see SceneComponent::SpawnInfo::ChildSpawnInfo.
    std::string displayName;
};

// Registered prefab name the entity was spawned from, read through its spawn template, or an empty
// string for inline/editor-authored entities (which have no prefab name). See EntitySpawnTemplate.
export inline const std::string& entityPrefabName(const Entity* entity)
{
    static const std::string empty;
    return entity->spawnTemplate ? entity->spawnTemplate->prefabName : empty;
}

// Registered prefab name the entity was spawned from, read through its spawn template, or an empty
// string for inline/editor-authored entities (which have no prefab name). See EntitySpawnTemplate.
export inline const std::string& entitySourceFile(const Entity* entity)
{
    static const std::string empty;
    return entity->spawnTemplate ? entity->spawnTemplate->sourceFile : empty;
}

// A single entity mutation requested through the editor UI, drained once per frame by the app, which
// owns the World (the spawner) and the root-entity list. The two Create cases carry the asset path to
// spawn (the UI has no World access); Delete/Reparent carry an owning handle that keeps the entity
// alive across the hand-off so the app can reconcile its own list by matching pointers.
export struct EntityChange
{
    // Asset (mesh or .pre) dropped onto the Scene hierarchy: spawn it and parent under `parent`
    // (nullptr = a top-level root the app owns). No screen position — placement comes from the parent.
    struct CreateHierarchy
    {
        std::string path;
        EntityPtr   parent;   // nullptr = World root
    };
    // Asset dropped onto the Viewport: spawn it at the world point under `screenPos` as a top-level root.
    struct CreateViewport
    {
        glm::ivec2  screenPos;
        std::string path;
    };
    struct Delete
    {
        EntityPtr entity;
    };
    // The entity was reparented in the panel; newParent == nullptr means it became a top-level root.
    struct Reparent
    {
        EntityPtr entity;
        EntityPtr newParent; // nullptr = root
    };
    // An entity subtree dropped onto the asset browser to (over)write as a prefab (.pre) at `path`. The
    // app writes it and refreshes spawn templates so the change takes effect without a restart. `root`
    // is an owning handle that keeps the subtree alive across the deferred hand-off.
    struct SavePrefab
    {
        EntityPtr   root;
        std::string path;
    };
    std::variant<CreateHierarchy, CreateViewport, Delete, Reparent, SavePrefab> type;
};