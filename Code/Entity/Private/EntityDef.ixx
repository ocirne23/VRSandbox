export module Entity:Entity;

import Core;
import Core.glm;
import Core.Transform;
import File.fwd;
import RendererVK.fwd;

export struct EntitySpawnTemplate;
export struct EntityPtr;

export enum EEntityFlags : uint8
{
    EEntityFlag_PrefabInstance = 1 << 0, // root of a locked prefab instance; cleared by "unpack"
};

export enum EComponentID : uint16
{
    EComponentID_Scene    = 0,
    EComponentID_Zone     = 1,
    EComponentID_Cull     = 2,
    EComponentID_Render   = 3,
    EComponentID_Animator = 4,

    EComponentID_GameLogic = 5,
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
    std::string displayName;
    const EntitySpawnTemplate* spawnTemplate = nullptr;

    uint16 refCount = 0;
    uint16 typeBits = 0;
    uint8 ecsComponentCount = 0;
    uint8 flags = 0; // EEntityFlags bitmask


    void serializeComponent(EComponentID id, AssetNode& out);
    void deserializeComponent(EComponentID id, const AssetNode& in);
    void reparentEntity(Entity* newParent);

    // Recursively submits this entity's RenderComponent (and its children's) to the renderer, and ticks any
    // AnimatorComponent (advancing animation + pushing the skinning palette) with deltaSeconds.
    void renderTree(Renderer& renderer, const Transform& parentWorld, float deltaSeconds = 0.0f);

    bool isPrefabInstance() const { return (flags & EEntityFlag_PrefabInstance) != 0; }
    void setPrefabInstance(bool on) { flags = on ? uint8(flags | EEntityFlag_PrefabInstance) : uint8(flags & ~EEntityFlag_PrefabInstance); }
    bool isPrefabLocked() const;
    Entity* nearestPrefabInstance();

    const std::string& getPrefabName() const;
    const std::string& getSourceFile() const;

private:
	Entity() = default;
	Entity(const Entity&) = delete;
    ~Entity() { assert(refCount == 0); }

    void createComponent(EComponentID id, uint16 componentOffset, const void* info, const Transform& base);
    void destroyComponent(EComponentID id, uint16 componentOffset, const void* info);
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