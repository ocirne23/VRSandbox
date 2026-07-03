export module Entity:Component;

import :Entity;
import :AnimationDescription;
import Core;
import Core.glm;
import Core.Transform;
import File;
import Animation;
import Script;
import Physics;

import RendererVK;

export int componentIdFromName(std::string_view name);
export void detachFromOwner(Entity* entity);

export constexpr uint16 MaxInlineComponentTypes = 7;
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
        bool skinned = false;                       // spawn a skinned node (GPU skinning) instead of a static one
        std::string spawnableName;                  // set when referenced via StaticMesh/SkinnedMesh (re-serialization)
    };

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);

    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}
};

// Drives a sibling skinned RenderComponent: instantiates an AnimationPlayer + AnimStateMachine from a
// .apl AnimatorDesc, retargets its clips against the rig skeleton, ticks them each frame, and pushes the
// resulting bone palette to the renderer. Gameplay sets parameters via stateMachine.setFloat/Bool/Trigger.
export struct AnimatorComponent
{
    static constexpr EComponentID getId() { return EComponentID_Animator; }

    ~AnimatorComponent();

    AnimationPlayer player;
    AnimStateMachine stateMachine;
    const AnimationSet* clipSet = nullptr;  // shared, World-cached clip library (retargeted to the rig)
    std::vector<BlendSpace1D> blendSpaces;  // stable storage referenced by the state machine
    std::vector<AnimatorDesc::SpeedBinding> stateSpeeds; // playback-speed config per StateId
    AnimatorDesc::SpeedBinding defaultSpeed;             // animator-wide playback-speed fallback
    std::function<void(const std::string&)> onEvent;    // gameplay hook for clip event notifies
    bool enabled = true;
    bool hasStateMachine = false;
    bool built = false;

    struct SpawnInfo
    {
        const AnimatorDesc* desc = nullptr;     // parsed .apl graph (owned by AssetRegistry)
        const Skeleton* skeleton = nullptr;     // rig skeleton from the sibling render mesh's container
        const AnimationSet* clipSet = nullptr;  // shared clip library (World-cached per skeleton+animator)
        std::string animatorName;               // kept for re-serialization
        bool enabled = true;
    };

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);
    void update(Entity& entity, Renderer& renderer, float deltaSeconds);
    float resolvePlaybackSpeed() const; // playback rate for the current state (param-driven or constant)

    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}
};

// References a visual script (.scr) the entity runs each frame. The Script library compiles the file on
// demand and ticks it with this entity as `self`, so the script's Get/Set Entity nodes read and write
// this entity's fields. Holds no execution state itself (Entity must not depend on the Script library).
export struct ScriptComponent
{
    static constexpr EComponentID getId() { return EComponentID_Script; }

    const ScriptModule* scriptModule = nullptr;
    std::unique_ptr<uint8[]> scriptData;
    uint32 scriptDataSize = 0;
    bool enabled = true;

    struct SpawnInfo
    {
        std::string scriptPath;
        bool enabled = true;
    };

    void spawn(Entity&, const SpawnInfo& info, const Transform&);
    void destroy(Entity&, const SpawnInfo&);

    // Compiles (on demand) and runs the referenced script with `entity` as its `self`. Defined in
    // ScriptRuntime.cpp, which owns the ScriptContext + thunks.
    void update(Entity& entity, float deltaSeconds);

    // Fires a named On Event entry (e.g. an animation notify). No-ops if the script declares no such entry.
    void fireEvent(Entity& entity, const std::string& eventName);

    void serialize(AssetNode& out) const
    {
        //if (scriptModule && scriptModule->scriptPath) out.set("Path", scriptModule->scriptPath);
        //if (!enabled)                                 out.set("Enabled", enabled);
    }
    void deserialize(const AssetNode& in)
    {
        //if (const AssetNode* n = in.find("Path"))    scriptPath = n->asString();
        //if (const AssetNode* n = in.find("Enabled")) enabled = n->asBool();
    }
};

// A rigid body simulated by the Physics library. Dynamic bodies drive the entity's transform after
// each step; kinematic and static bodies follow the entity when it is moved (gizmo, scripts). The
// entity's world scale is baked into the collision shape at spawn.
export struct PhysicsComponent
{
    static constexpr EComponentID getId() { return EComponentID_Physics; }

    PhysicsBody body;
    Transform lastWorld;      // last world transform pushed to a non-dynamic body (change detection)
    EPhysicsBodyType bodyType = EPhysicsBodyType::Dynamic;
    bool enabled = true;
    bool synced = false;      // body is teleported to the entity's true world transform on first update

    struct SpawnInfo
    {
        EPhysicsBodyType bodyType = EPhysicsBodyType::Dynamic;
        PhysicsShape shape;                    // filter bits resolved from the names below at parse time
        std::string layer;                     // named collision layer (category), empty = Default
        std::vector<std::string> collidesWith; // named layers this body collides with ("All"/"None" allowed), empty = all
        bool enabled = true;
    };

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);
    void update(Entity& entity, const Transform& parentWorld);

    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}
};

export Transform composeTransform(const Transform& parent, const Transform& local);

export const RenderComponent::SpawnInfo* getRenderSpawnInfo(const Entity* entity);
export const AnimatorComponent::SpawnInfo* getAnimatorSpawnInfo(const Entity* entity);
export const PhysicsComponent::SpawnInfo* getPhysicsSpawnInfo(const Entity* entity);

// Serializes a render spawn recipe into a "Component Render" node; mirror of World::buildRenderSpawnInfo.
export void writeRenderSpawnInfo(const RenderComponent::SpawnInfo& info, AssetNode& out);

// Serializes an animator spawn recipe into a "Component Animator" node.
export void writeAnimatorSpawnInfo(const AnimatorComponent::SpawnInfo& info, AssetNode& out);

// Serializes a physics spawn recipe into a "Component Physics" node.
export void writePhysicsSpawnInfo(const PhysicsComponent::SpawnInfo& info, AssetNode& out);

export constexpr const char* componentTypeName(EComponentID id)
{
    switch (id)
    {
    case EComponentID_Scene:  return "Scene";
    case EComponentID_Zone:   return "Zone";
    case EComponentID_Cull:   return "Cull";
    case EComponentID_Render: return "Render";
    case EComponentID_Animator: return "Animator";
    case EComponentID_Script: return "Script";
    case EComponentID_Physics: return "Physics";
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
        alignUp(uint16(sizeof(AnimatorComponent)), ComponentAlignment),
        alignUp(uint16(sizeof(ScriptComponent)),  ComponentAlignment),
        alignUp(uint16(sizeof(PhysicsComponent)), ComponentAlignment),
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
