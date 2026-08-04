export module Entity:NetworkComponent;

import :Entity;
import Core;
import Core.glm;
import Core.Transform;
import File;

// Snapshot record flags (the wire's per-record `recFlags` byte, mirrored into `targetFlags`).
// A decoder that meets a bit it doesn't know drops the message — it cannot know the record's size.
export constexpr uint8 NetRecFlag_Physics = 1 << 0; // record carries body WORLD pose + velocities (else entity LOCAL pos/rot)
export constexpr uint8 NetRecFlag_Asleep  = 1 << 1; // the server's body is asleep: hard-sync once and sleep too

// Marks the entity for server->client sync ("Component Network" in .pre files). The entity pairs with
// its counterpart on the other side by netId — authored (`Id <n>`) or auto-derived from the root-to-
// entity name path at spawn (both sides load the same scene this milestone; there is no spawn
// replication yet, and bit 31 of the id space is reserved for it). On the server the component only
// carries send bookkeeping (NetworkManager reads the live state on the main thread); on a client
// update() corrects toward the latest received target by the "Network/Correction" thresholds
// (deadzone / exponential blend / hard snap). DYNAMIC-body entities sync the BODY: the server
// snapshots the body's world pose + velocities, and the client corrects through the thread-safe
// PhysicsWorld command queue (teleport + velocity sets; velocities/sleep apply once per snapshot,
// the pose blend runs continuously) — PhysicsComponent::update rewrites entity.pos/rot from the body
// right after, so correcting the entity directly would be overwritten. Kinematic/static bodies use
// the entity-transform path (the collider stays put — teleportBody is the engine-wide rule).
export struct NetworkComponent
{
    static constexpr EComponentID getId() { return EComponentID_Network; }

    uint32 netId = 0;

    // client: latest server state, written by NetworkManager::receive on the main thread BEFORE the
    // (parallel) entity pass — update() only reads it, so no synchronization is needed
    glm::vec3 targetPos = glm::vec3(0.0f);   // body WORLD pos when NetRecFlag_Physics, else entity LOCAL pos
    glm::quat targetRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 targetLinVel = glm::vec3(0.0f);
    glm::vec3 targetAngVel = glm::vec3(0.0f); // radians/second about each world axis
    uint32 serverTick = 0;          // tick of the applied record (per-entity stale-drop for reordered packets)
    uint32 lastAppliedTick = 0;     // one-shot latch: velocities/sleep apply once per new snapshot
    float timeSinceSnapshot = 0.0f;
    uint8 targetFlags = 0;          // NetRecFlag bits of the applied record
    bool hasTarget = false;

    // server: change detection against the last sent state (non-physics), awake->asleep edge (physics)
    glm::vec3 lastSentPos = glm::vec3(FLT_MAX);
    glm::quat lastSentRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bool sleepDirty = true; // body has been awake since the last asleep record: send one final record at rest

    struct SpawnInfo
    {
        uint32 id = 0; // 0 = auto-derive from the entity's name path; authored ids must be < 0x80000000
    };

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);
    void update(Entity& entity, float deltaSeconds); // client correction toward the latest target
};

export const NetworkComponent::SpawnInfo* getNetworkSpawnInfo(const Entity* entity);

// Serializes a network spawn recipe into a "Component Network" node.
export void writeNetworkSpawnInfo(const NetworkComponent::SpawnInfo& info, AssetNode& out);
