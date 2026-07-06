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
    EEntityFlag_EditorPaused   = 1 << 1, // open in the Entity Editor: scripts/physics don't update this tree
};

export enum EComponentID : uint16
{
    EComponentID_Scene    = 0,
    EComponentID_Zone     = 1,
    EComponentID_Cull     = 2,
    EComponentID_Render   = 3,
    EComponentID_Animator = 4,
    EComponentID_Physics  = 5,
    EComponentID_Audio    = 6,
    EComponentID_Script   = 7, // should be last so all other components are available on spawn
};

export class Entity
{
public:

    // initialFlags (EEntityFlags) applies before components spawn, so e.g. EEntityFlag_EditorPaused can
    // suppress a script's OnSpawn when the Entity Editor respawns a paused entity.
    static EntityPtr create(const EntitySpawnTemplate& tmpl, const Transform& transform, uint8 initialFlags = 0);
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

    void update(Renderer& renderer, float deltaSeconds) { updateTree(renderer, Transform(), deltaSeconds); }

    void serializeComponent(EComponentID id, AssetNode& out);
    void deserializeComponent(EComponentID id, const AssetNode& in);
    void reparentEntity(Entity* newParent);

    bool isPrefabInstance() const { return (flags & EEntityFlag_PrefabInstance) != 0; }
    void setPrefabInstance(bool on) { flags = on ? uint8(flags | EEntityFlag_PrefabInstance) : uint8(flags & ~EEntityFlag_PrefabInstance); }
    bool isEditorPaused() const { return (flags & EEntityFlag_EditorPaused) != 0; }
    void setEditorPaused(bool on) { flags = on ? uint8(flags | EEntityFlag_EditorPaused) : uint8(flags & ~EEntityFlag_EditorPaused); }
    // The flag lives on the edited document's root; entry points invoked outside updateTree's propagation
    // (global script events, physics contact events) must check the whole ancestor chain.
    bool isEditorPausedInTree() const
    {
        for (const Entity* p = this; p; p = p->parent)
            if (p->isEditorPaused())
                return true;
        return false;
    }
    bool isPrefabLocked() const;
    Entity* nearestPrefabInstance();

    const std::string& getPrefabName() const;
    const std::string& getSourceFile() const;

private:
	Entity() = default;
	Entity(const Entity&) = delete;
    ~Entity() { assert(refCount == 0); }

    void updateTree(Renderer& renderer, const Transform& parentWorld, float deltaSeconds = 0.0f, bool editorPaused = false);

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
        std::string text; // pre-serialized document (Entity Editor's transform draft); empty → serialize root
    };
    struct OpenPrefabForEdit
    {
        std::string path; // .pre to load and unpack for the Prefab Editor
    };
    struct NewPrefab
    {
        std::string displayName; // blank editable entity, name only (no source file yet)
    };
    struct RespawnEntity
    {
        EntityPtr oldEntity;
        std::shared_ptr<const EntitySpawnTemplate> tmpl; // freshly assembled from the entity's edited component set
    };
    std::variant<CreateHierarchy, CreateViewport, AddSceneEntity, Delete, Reparent, SavePrefab, OpenPrefabForEdit, NewPrefab, RespawnEntity> type;
};