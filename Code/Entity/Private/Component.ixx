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
import Audio;

import RendererVK;

export int componentIdFromName(std::string_view name);
export void detachFromOwner(Entity* entity);

export constexpr uint16 MaxInlineComponentTypes = 8;
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

    ~RenderComponent() {}

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
        std::string rigType;                        // skinned only: "Humanoid" / "Generic" (empty = unspecified; informational, not yet consumed)
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
    bool pendingOnSpawn = false; // OnSpawn was suppressed (spawned editor-paused); fired when editing ends

    struct SpawnInfo
    {
        std::string scriptPath;
        bool enabled = true;
    };

    void spawn(Entity&, const SpawnInfo& info, const Transform&);
    void destroy(Entity&, const SpawnInfo&);

    // Runs a suppressed OnSpawn (see pendingOnSpawn). Call after clearing the editor-paused flag.
    void fireOnSpawnIfPending(Entity& entity);

    // Compiles (on demand) and runs the referenced script with `entity` as its `self`. Defined in
    // ScriptRuntime.cpp, which owns the ScriptContext + thunks.
    void update(Entity& entity, float deltaSeconds);

    // Fires a named On Event entry (e.g. an animation notify). No-ops if the script declares no such entry.
    void fireEvent(Entity& entity, const std::string& eventName);
    void fireEvent(Entity& entity, uint32 eventKey); // Globals::scriptEvents.getEventKeyForName

    // Fires the On Physics Event entry point (see dispatchPhysicsContactEvents). No-ops if the script
    // declares no such entry. contactId is only valid for the frame it was collected (see PhysicsWorld::getContactPoint).
    void firePhysicsEvent(Entity& entity, Entity* other, bool begin, bool sensor, int64 contactId);

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
// each step (interpolated between fixed steps); kinematic and static bodies follow the entity when
// it is moved (gizmo, scripts). The entity's world scale is baked into the collision shape at spawn.
export struct PhysicsComponent
{
    static constexpr EComponentID getId() { return EComponentID_Physics; }

    PhysicsBody body;
    Transform lastWorld;      // last world transform pushed to a non-dynamic body (change detection)
    glm::vec3 prevPos, currPos; // body pose at the previous/current physics step (dynamic interpolation)
    glm::quat prevRot, currRot;
    uint32 lastStep = 0;
    EPhysicsBodyType bodyType = EPhysicsBodyType::Dynamic;
    bool enabled = true;
    bool synced = false;      // body is teleported to the entity's true world transform on first update

    // Fired by dispatchPhysicsContactEvents for begin/end contact and sensor overlaps involving this
    // body (the shape must set ContactEvents true, or be a Sensor). C++ gameplay hook; scripts get
    // the same events through the "On Physics Event" node.
    std::function<void(Entity& other, bool begin)> onContact;

    struct SpawnInfo
    {
        EPhysicsBodyType bodyType = EPhysicsBodyType::Dynamic;
        PhysicsShape shape;                    // filter bits / geometry resolved from the fields below at parse time
        std::string layer;                     // named collision layer (category), empty = Default
        std::vector<std::string> collidesWith; // named layers this body collides with ("All"/"None" allowed), empty = all
        std::shared_ptr<PhysicsMesh> mesh;     // Shape Mesh: keeps the shared collision BVH alive (shape.mesh points at it)
        bool enabled = true;
    };

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);
    void update(Entity& entity, const Transform& parentWorld);

    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}
};

// How a Sound alias holding several clips picks which one to play on each trigger.
export enum class EAudioSelect : uint8
{
    Single,           // always play clips[0] (a one-clip sound; the default)
    Random,           // a uniformly random clip each trigger (may repeat)
    RandomNoRepeat,   // uniformly random, but never the same clip twice in a row
    Cycle,            // step through the clips in order, wrapping around
    CycleStartRandom, // Cycle but starting index is randomized
};

export const char* audioSelectToken(EAudioSelect select);
export EAudioSelect audioSelectFromToken(std::string_view token);

// A set of named, triggerable sounds ("Component Audio" in .pre files): each Sound entry pairs an
// alias with one or more sound-file clips (each with its own playback settings) and a Select mode that
// picks a clip per trigger. Gameplay (or the script "Trigger Audio" node, via ctx->entityTriggerAudio)
// plays one by alias; playing spatial sounds follow the entity unless the trigger supplied a position.
export struct AudioComponent
{
    static constexpr EComponentID getId() { return EComponentID_Audio; }

    // One playable file behind a Sound alias, with its own settings. A `Path` line in the .pre, whose
    // child lines (Volume/Pitch/...) are these fields.
    struct Clip
    {
        std::string path;                         // sound file, relative to Assets/ (WAV/FLAC/MP3)
        std::shared_ptr<AudioBuffer> buffer;      // World-cached, shared between entities using the same file
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool relative = false;                    // 2D playback (no spatialization/attenuation)
        float referenceDistance = 1.0f;           // inverse-clamped attenuation (see AudioSource::setAttenuation)
        float maxDistance = FLT_MAX;
        float rolloff = 1.0f;
    };

    struct SoundDesc
    {
        std::string alias;                        // the name gameplay/scripts trigger it by
        EAudioSelect select = EAudioSelect::Single;
        std::vector<Clip> clips;
    };

    struct SpawnInfo
    {
        std::vector<SoundDesc> sounds;
    };

    // Per-trigger overrides for the authored settings; unset fields keep the selected clip's values. A
    // set position pins the sound at that world position instead of following the entity.
    struct TriggerOverrides
    {
        std::optional<glm::vec3> position;
        std::optional<float> volume;
        std::optional<float> pitch;
    };

    struct Voice // playback state per SoundDesc (parallel to info->sounds)
    {
        AudioSource source;   // created lazily on first trigger
        int currentClip = -1; // clip index currently loaded into source (-1 = none)
        int lastClip = -1;    // last clip played, for RandomNoRepeat
        int cycleNext = -1;   // next clip index for Cycle
        bool follow = true;   // track the entity's world position while playing
    };

    const SpawnInfo* info = nullptr;
    std::vector<Voice> voices;

    bool trigger(Entity& entity, std::string_view alias, const TriggerOverrides& overrides = {});
    void stopSound(std::string_view alias); // empty alias = stop every sound
    int findSound(std::string_view alias) const;
    std::span<const SoundDesc> getSounds() const { return info ? std::span<const SoundDesc>(info->sounds) : std::span<const SoundDesc>(); }

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);
    void update(Entity& entity, const Transform& world); // playing follow-sounds track the entity

    void serialize(AssetNode&) const {}
    void deserialize(const AssetNode&) {}

private:
    int selectClip(const SoundDesc& sound, Voice& voice) const;
};

// Maps this frame's physics contact/sensor events (body userData -> Entity*) onto the involved
// entities: invokes PhysicsComponent::onContact and fires the script's "On Physics Event" entry
// point (ScriptComponent::firePhysicsEvent). Call once per frame after Globals::physics.update().
export void dispatchPhysicsContactEvents();

export Transform composeTransform(const Transform& parent, const Transform& local);

export const RenderComponent::SpawnInfo* getRenderSpawnInfo(const Entity* entity);
export const AnimatorComponent::SpawnInfo* getAnimatorSpawnInfo(const Entity* entity);
export const PhysicsComponent::SpawnInfo* getPhysicsSpawnInfo(const Entity* entity);
export const AudioComponent::SpawnInfo* getAudioSpawnInfo(const Entity* entity);
export const ScriptComponent::SpawnInfo* getScriptSpawnInfo(const Entity* entity);

// Serializes a render spawn recipe into a "Component Render" node; mirror of World::buildRenderSpawnInfo.
export void writeRenderSpawnInfo(const RenderComponent::SpawnInfo& info, AssetNode& out);

// Serializes an animator spawn recipe into a "Component Animator" node.
export void writeAnimatorSpawnInfo(const AnimatorComponent::SpawnInfo& info, AssetNode& out);

// Serializes a physics spawn recipe into a "Component Physics" node.
export void writePhysicsSpawnInfo(const PhysicsComponent::SpawnInfo& info, AssetNode& out);

// Serializes an audio spawn recipe into a "Component Audio" node.
export void writeAudioSpawnInfo(const AudioComponent::SpawnInfo& info, AssetNode& out);

export constexpr const char* componentTypeName(EComponentID id)
{
    switch (id)
    {
    case EComponentID_Scene:  return "Scene";
    case EComponentID_Zone:   return "Zone";
    case EComponentID_Cull:   return "Cull";
    case EComponentID_Render: return "Render";
    case EComponentID_Animator: return "Animator";
    case EComponentID_Physics: return "Physics";
    case EComponentID_Audio:  return "Audio";
    case EComponentID_Script: return "Script";
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
        alignUp(uint16(sizeof(PhysicsComponent)), ComponentAlignment),
        alignUp(uint16(sizeof(AudioComponent)),   ComponentAlignment),
        alignUp(uint16(sizeof(ScriptComponent)),  ComponentAlignment),
    };
    static_assert(EComponentID_Scene == 0);
    static_assert(EComponentID_Zone == 1);
    static_assert(EComponentID_Cull == 2);
    static_assert(EComponentID_Render == 3);
    static_assert(EComponentID_Animator == 4);
    static_assert(EComponentID_Physics == 5);
    static_assert(EComponentID_Audio == 6);
    static_assert(EComponentID_Script == 7);

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
