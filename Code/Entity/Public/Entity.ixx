export module Entity;

import Core;
import Core.glm;
import Core.Transform;
import File.fwd;

export struct EntitySpawnTemplate;
export struct EntityPtr;

export enum EEntityFlags : uint8
{
    EEntityFlag_PrefabInstance = 1 << 0, // root of a locked prefab instance; cleared by "unpack"
};

export enum EComponentID : uint16
{
    EComponentID_Scene  = 0,
    EComponentID_Zone   = 1,
    EComponentID_Cull   = 2,
    EComponentID_Render = 3,

    EComponentID_GameLogic = 4,
};

export class Entity
{
public:

    static EntityPtr create(const EntitySpawnTemplate& tmpl, const Transform& transform);
    static void destroy(Entity* entity);

public:

    glm::vec3 pos;
    float scale = 1.0f;
    glm::quat rot;

    Entity* parent = nullptr;
    std::string name;
    const EntitySpawnTemplate* spawnTemplate = nullptr;

    uint16 refCount = 0;
    uint16 typeBits = 0;
    uint8 ecsComponentCount = 0;
    uint8 flags = 0; // EEntityFlags bitmask

    ~Entity() { assert(refCount == 0); }

    void serializeComponent(EComponentID id, AssetNode& out);
    void deserializeComponent(EComponentID id, const AssetNode& in);
    void reparentEntity(Entity* newParent);

    bool isPrefabInstance() const { return (flags & EEntityFlag_PrefabInstance) != 0; }
    void setPrefabInstance(bool on) { flags = on ? uint8(flags | EEntityFlag_PrefabInstance) : uint8(flags & ~EEntityFlag_PrefabInstance); }
    bool isPrefabLocked() const;
    Entity* nearestPrefabInstance();

    const std::string& getPrefabName() const;
    const std::string& getSourceFile() const;

private:

    void createComponent(EComponentID id, const void* info, const Transform& base);
    void destroyComponent(EComponentID id, const void* info);
};

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
            Entity::destroy(m_entity);
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

export struct EntitySpawnTemplate
{
    EntityArchetype archetype;
    Transform defaultTransform;
    std::vector<std::shared_ptr<void>> spawnInfos;
    std::string sourceFile;
    std::string prefabName;
    std::string displayName;
};

inline const std::string& Entity::getPrefabName() const
{
    static const std::string empty;
    return spawnTemplate ? spawnTemplate->prefabName : empty;
}

inline const std::string& Entity::getSourceFile() const
{
    static const std::string empty;
    return spawnTemplate ? spawnTemplate->sourceFile : empty;
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