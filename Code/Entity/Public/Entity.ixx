export module Entity;

import Core;
import Core.glm;
import Core.Transform;

export struct EntitySpawnTemplate;

export struct Entity
{
    glm::vec3 pos;
    float scale = 1.0f;
    glm::quat rot;

    Entity* parent = nullptr;
    std::string name;
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

export void destroyEntity(Entity* entity);

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
    operator Entity*() const { return m_entity; }
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

export struct EntityArchetype
{
    uint16 allocSize = 0;
    uint16 typeBits = 0;
};

export EntityArchetype makeEntityArchetype(uint16 typeBits);

export EntityPtr createEntity(const EntitySpawnTemplate& tmpl, const Transform& transform);

export struct EntitySpawnTemplate
{
    EntityArchetype archetype;
    Transform defaultTransform;
    std::vector<std::shared_ptr<void>> spawnInfos;
    std::string sourceFile;
    std::string prefabName;
    std::string displayName;
};

export inline const std::string& entityPrefabName(const Entity* entity)
{
    static const std::string empty;
    return entity->spawnTemplate ? entity->spawnTemplate->prefabName : empty;
}

export inline const std::string& entitySourceFile(const Entity* entity)
{
    static const std::string empty;
    return entity->spawnTemplate ? entity->spawnTemplate->sourceFile : empty;
}

export struct EntityChange
{
    struct CreateHierarchy
    {
        std::string path;
        EntityPtr   parent;   // nullptr = World root
    };
    struct CreateViewport
    {
        glm::ivec2  screenPos;
        std::string path;
    };
    struct AddSceneEntity
    {
        std::string displayName;
        EntityPtr parent;
    };
    struct Delete
    {
        EntityPtr entity;
    };
    struct Reparent
    {
        EntityPtr entity;
        EntityPtr newParent; // nullptr = root
    };
    struct SavePrefab
    {
        EntityPtr   root;
        std::string path;
    };
    std::variant<CreateHierarchy, CreateViewport, AddSceneEntity, Delete, Reparent, SavePrefab> type;
};