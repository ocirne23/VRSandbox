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
export constexpr uint8 NetRecFlag_Forced  = 1 << 2; // owner must accept this correction (claims were rejected); non-owners ignore it
export constexpr uint8 NetRecFlag_Arbitrated = 1 << 3; // player-vs-player contact: the server solver owns the pose — the owner softly corrects toward it while still steering (claims are velocity intent)

// Which simulation drives this entity, seen from the LOCAL process (derived, never stored):
// the same entity reads LocalOwner on the client that owns it and RemoteOwner on the server/everyone else.
export enum class ENetAuthority : uint8
{
    Local,       // netId == 0: not part of the session (single player / client-local content)
    ServerOwned, // ownerClientId == 0: the server's simulation is the authority
    LocalOwner,  // owned by THIS process (client): my claims drive it — simulate freely, obey only Forced
    RemoteOwner, // owned by another client: their claims drive it (the server validates, everyone else replicates)
};

// Outcome of the server's plausibility gate on the newest claim (debug/editor surface).
export enum class EClaimResult : uint8
{
    None,             // no claim seen yet
    Accepted,
    RejectedSpeed,    // displacement exceeded the per-tick speed budget
    RejectedVelocity, // claimed velocity magnitude over the cap
    RejectedPath,     // trajectory raycast hit world geometry (teleporting through walls)
    RejectedTeleport, // displacement over the hard cap regardless of elapsed time
};

// Buffered snapshot history for a REMOTE-OWNED entity (another player): the observer plays the
// owner's exact recorded trajectory a couple of ticks in the past instead of physics-chasing the
// newest state — a capped-accel body following a delayed, input-spiky signal inherently lag-chases
// starts and overshoots stops. Slots carry their own tick (ring wrap + packet loss validation).
// Owned by NetworkManager (stable unique_ptr storage), written main-thread in handleSnapshot,
// read by NetworkComponent::update on job workers — publish-then-read-only like the targets.
export struct NetSnapshotRecord
{
    uint32 tick = 0;
    glm::vec3 pos = glm::vec3(0.0f);
    glm::quat rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 linVel = glm::vec3(0.0f);
    glm::vec3 angVel = glm::vec3(0.0f);
};
export struct NetSnapshotRing
{
    static constexpr uint32 Capacity = 16;
    NetSnapshotRecord records[Capacity]; // indexed tick % Capacity
    uint32 newestTick = 0;
    uint32 count = 0;
};

// The owner's sampled intent, streamed with each claim. Symmetric: the owning client's controller
// writes it locally, the server stores the latest accepted one on its twin so server-side gameplay
// can read the player's input without extra plumbing.
export constexpr uint32 NetInputButton_Jump = 1 << 0;
export struct NetInputState
{
    uint32 buttons = 0;
    glm::vec3 move = glm::vec3(0.0f);
    glm::vec3 look = glm::vec3(0.0f);
};

// Marks the entity as SERVER-OWNED networked state ("Component Network" in .pre files — pure
// PRESENCE, no authored data: netIds are a code abstraction minted only by the server). On the
// server, registration (scene load or runtime alike) assigns an id and replicates the spawn to every
// client; on a client, the component either carries the server's id (spawned by a replicated Spawn)
// or stays LOCAL-INERT with netId 0 (client-local content — never synced, never conflicts). On the
// server the component only carries send bookkeeping (NetworkManager reads the live state on the
// main thread); on a client
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

    uint32 netId = 0;         // 0 = local-inert (client-local content / single player); anything else is server-minted
    uint32 ownerClientId = 0; // 0 = server-owned; else the clientId whose claims drive this entity (set via NetworkManager::setOwner, carried in the Spawn message — never authored)

    // Local, ServerOwned, LocalOwner or RemoteOwner — from THIS process's perspective (see the enum)
    ENetAuthority authority() const;

    NetInputState input; // owner writes its sampled intent here; the server mirrors the latest accepted one

    // server-side validation state (meaningful only for client-owned entities, server role)
    uint32 lastClaimSeq = 0;         // newest claim seq seen (dedups the redundant resends)
    uint32 lastAcceptedClaimSeq = 0;
    glm::vec3 lastAcceptedClaimPos = glm::vec3(0.0f); // displacement-budget anchor: what was last ACCEPTED, not the live twin (contacts/corrections perturb it)
    EClaimResult lastClaimResult = EClaimResult::None;
    uint16 violations = 0;           // rejected-claim count (saturating) — cheat telemetry
    uint32 forcedUntilTick = 0;      // while serverTick < this, this entity's snapshot records carry NetRecFlag_Forced

    // owner-side (client): next outgoing claim sequence (the redundancy ring lives in the manager —
    // only a handful of entities are ever locally owned, the inline component shouldn't tax them all)
    uint32 claimSeq = 0;

    // client: seconds of correction suspension left because one of OUR claim-driven bodies is (or
    // just was) touching this server-owned body — corrections would fight the player's push with the
    // server's RTT-old pre-push state (see NetSyncParams::interactionRadius)
    float localInteractionGrace = 0.0f;

    // Ownership TRANSFER (server-decided, proximity-based — see NetworkManager::updateOwnershipTransfers):
    // ownership acquired via OwnerChange rather than the entity's Spawn. On the server it marks a
    // release candidate (reverts to server-owned once away from the owner's PRIMARY bodies); on the
    // owning client it marks "not my primary entity" — player control drives only primaries, while
    // the claim stream carries both.
    bool transferredOwnership = false;
    float releaseTimer = 0.0f; // server: seconds spent outside the owner's release radius

    // Server ARBITRATION (see NetworkManager::stealOwnershipOnContact):
    // primaries: while serverTick < arbitratedUntilTick (player-vs-player contact, refreshed per
    // contact) the twin's pose is solver-owned — claims apply as bounded velocity nudges and the
    // owner's records carry NetRecFlag_Arbitrated. Objects: the last two DISTINCT clients to touch,
    // with ages — both fresh = CONTESTED, the object reverts to server ownership until the window
    // decays (single-owner transfer behavior resumes after).
    uint32 arbitratedUntilTick = 0;
    uint32 contestClients[2] = { 0, 0 };
    float contestAges[2] = { 0.0f, 0.0f };
    uint32 contestedUntilTick = 0;

    // client: latest server state, written by NetworkManager::receive on the main thread BEFORE the
    // (parallel) entity pass — update() only reads it, so no synchronization is needed
    glm::vec3 targetPos = glm::vec3(0.0f);   // body WORLD pos when NetRecFlag_Physics, else entity LOCAL pos
    glm::quat targetRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 targetLinVel = glm::vec3(0.0f);
    glm::vec3 targetAngVel = glm::vec3(0.0f); // radians/second about each world axis
    uint32 serverTick = 0;          // tick of the applied record (per-entity stale-drop for reordered packets)
    uint32 lastAppliedTick = 0;     // one-shot latch: velocities/sleep apply once per new snapshot
    float timeSinceSnapshot = 0.0f;
    // remote-owned entities: interpolation playback state (see NetSnapshotRing above)
    NetSnapshotRing* remoteBuffer = nullptr; // manager-owned, set by handleSnapshot (main thread)
    float playbackTick = 0.0f;               // fractional tick the observer is currently displaying
    uint8 targetFlags = 0;          // NetRecFlag bits of the applied record
    bool hasTarget = false;

    // server: change detection against the last sent state (non-physics), awake->asleep edge (physics)
    glm::vec3 lastSentPos = glm::vec3(FLT_MAX);
    glm::quat lastSentRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bool sleepDirty = true; // body has been awake since the last asleep record: send one final record at rest

    struct SpawnInfo {}; // presence only — ids are assigned in code (see registerEntity), never authored

    void spawn(Entity& entity, const SpawnInfo& info, const Transform& base);
    void destroy(Entity& entity, const SpawnInfo& info);
    void update(Entity& entity, float deltaSeconds); // client correction toward the latest target
};

export const NetworkComponent::SpawnInfo* getNetworkSpawnInfo(const Entity* entity);

// Serializes a network spawn recipe into a "Component Network" node.
export void writeNetworkSpawnInfo(const NetworkComponent::SpawnInfo& info, AssetNode& out);
