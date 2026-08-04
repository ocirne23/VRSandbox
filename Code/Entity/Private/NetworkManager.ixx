export module Entity:NetworkManager;

import Core;
import Core.glm;
import Network;
import :Entity;
import :NetworkComponent; // NetInputState in the claim ring; no cycle — the component never imports the manager

// Server-authoritative entity sync over the Network library's reliable-UDP NetHost. One manager per
// process, role picked at startup: Server opens a listening host, Client connects out, None (single
// player) leaves everything inert.
//
// OWNERSHIP MODEL: every networked entity is SERVER-OWNED. A NetworkComponent registering on the
// server — scene load or runtime, no distinction — gets a server-assigned netId and a spawn record,
// and materializes on every client through a reliable Spawn message (late joiners get all live
// records replayed after their Welcome). Clients never invent ids: a NetworkComponent registering
// client-side OUTSIDE an incoming Spawn (shared prefab reused as client-local content) stays
// LOCAL-INERT — netId 0, not in the registry, never synced. Client-only and server-only entities are
// simply entities without a NetworkComponent (or client-local ones); the two scenes can differ
// arbitrarily because nothing outside the networked set needs to correspond, and identity is the
// server's id alone. Shared static scenery that doesn't move needs no NetworkComponent at all — only
// STATE is networked, and the server streams pos/rot snapshots for registered entities at
// "Network/Snapshot Hz" which clients correct toward (NetworkComponent::update). Named script events
// travel the same connection (fireNetworkEvent): fired locally, sent to the other side, and relayed
// server-side to every other client — so an "On Event" script entry reacts identically everywhere.
//
// Threading: every NetHost call happens on the main thread (receive() before the entity/physics
// updates, send() after world.update — see main.cpp). Snapshot decode writes component target state
// on the main thread too, so NetworkComponent::update (parallel entity pass) only ever reads it.
// The registry mutex is insurance for spawn/destroy, which are main-thread today.
//
// Wire protocol (GameProtocolId bumps on ANY format change — the transport handshake denies
// mismatched ids, which is the version gate):
//   ch0 Unreliable:  Snapshot [u8][varuint serverTick][u8 flags bit0=quantized]
//                    [f32 maxVel][f32 maxAngVel (quantized only — the velocity quantization ranges
//                    are live tweaks, so the decoder must be told per message)][u16 count] + records
//                    record: [varuint netId][u8 recFlags (NetRecFlag bits)][pos 3xf32][rot u32 smallest-three | 4xf32]
//                    + when NetRecFlag_Physics: [linVel][angVel], each 3xu16 quantized over ±range | 3xf32.
//                    Physics records carry the BODY's world pose; non-physics records the entity's
//                    LOCAL pos/rot. Chunked under "Snapshot max bytes"; every chunk repeats the tick,
//                    staleness is dropped PER ENTITY (record tick <= last applied), so chunk
//                    reordering is harmless (UnreliableSequenced would drop sibling chunks instead).
//   ch1 Reliable:    Event [u8][varuint senderClientId (server->client ONLY)][string name] — a
//                    client sends no id: its identity is the connection, and an ignored
//                    client-writable one is a refactor away from being trusted
//   ch2 Reliable:    Hello (client->server) [u8][u16 version]
//                    Welcome (server->client) [u8][u16 version][varuint serverTick][f32 snapshotHz][varuint yourClientId]
//                    Deny [u8][string reason]
//                    Spawn (server->client) [u8][varuint baseId][varuint componentCount][varuint ownerClientId][string path][pos 3xf32][rot 4xf32][f32 scale]
//                    Despawn (server->client) [u8][varuint baseId]
//   ch3 Unreliable:  Claim (owner->server) [u8][varuint netId][varuint newestSeq][u8 count]
//                    [u8 flags bit0=quantized][f32 maxVel][f32 maxAngVel (quantized only — the
//                    owner's live ranges, so the server decodes with what was encoded)] + count
//                    records oldest->newest, each [u32 buttons][move 3xf32][pos 3xf32]
//                    [rot u32 smallest-three | 4xf32][linVel + angVel 3xu16 over ±range | 3xf32]
//                    + ONE trailing [look 3xf32] applied to every record (it barely changes across
//                    the window). The last "Claim redundancy" claims ride in EVERY packet
//                    (redundancy beats a reliable channel under loss: no head-of-line stall), the
//                    server dedups by seq. The server VALIDATES each claim (Network/Validation tweaks:
//                    a wall-clock movement token bucket refilled at max speed, velocity cap,
//                    trajectory raycast, hard teleport cap) — accepted claims overwrite its twin and flow to the
//                    other clients as normal snapshots; rejections bump `violations` and arm
//                    NetRecFlag_Forced on the owner's records so the owner gets dragged back.
// Spawn/Despawn ride the session channel so they order after Welcome; a late joiner gets every live
// spawn record replayed right after its Welcome — that replay IS how the networked world arrives.

export enum class ENetRole : uint8
{
    None,   // single player: no socket, fireNetworkEvent degrades to a local fireEvent
    Server, // authoritative: accepts clients, streams snapshots
    Client, // corrects local entities toward server snapshots
};

// Client-side correction thresholds, shared with NetworkComponent::update (registered as tweaks).
export struct NetSyncParams
{
    float posDeadzone = 0.01f;        // below: local state free-runs
    float posSnapThreshold = 2.0f;    // entering the push's CATCH-UP band (boosted gains/caps)
    float rotDeadzoneDeg = 0.5f;
    float rotSnapThresholdDeg = 45.0f;
    float blendRate = 10.0f;          // NON-physics entities: exponential blend rate toward the target
    bool extrapolate = true;          // dead-reckon the pose target by linVel * timeSinceSnapshot
    // REMOTE-OWNED entities (other players) skip the chase and replay the owner's recorded
    // trajectory this many snapshot ticks in the past, interpolating between buffered snapshots.
    // Tweak minimum is 2 — the cursor clamps to `newest - 1`, so 1 leaves it no headroom and
    // playback advances only on snapshot arrival. Loss gaps fall through to the push for a frame.
    int remoteInterpTicks = 2;

    // PHYSICAL PUSH (the correction for dynamic bodies): between deadzone and snap the body is
    // steered by bounded impulses through the sim — position/rotation error becomes corrective
    // velocity on top of the server's, applied via EBodyCommand::NudgeVelocity with a per-second
    // acceleration cap. Past the snap threshold the push enters CATCH-UP: gains and caps are
    // multiplied by pushCatchUpBoost so multi-meter errors still correct THROUGH the sim — a body
    // plows around obstacles with legitimate contacts instead of teleporting into them (a teleport
    // into an occupied space would depenetration-fling both bodies into fresh desync). The true
    // teleport resync survives as the LAST resort, at posTeleportThreshold only (rotation alone
    // never teleports — a wrong orientation can't materialize inside anything).
    // LOCAL INTERACTION GRACE: while a server-owned body sits within this radius of one of OUR
    // claim-driven bodies, the client suspends its corrections — they would drag it toward the
    // server's RTT-old (pre-push) state and fight the shove the player is applying right now. The
    // server receives the same push ~an RTT later through the claim-followed player twin, and the
    // normal corrections reconcile the residue once the grace lingers out. 0 = off.
    float interactionRadius = 1.5f;
    float interactionLinger = 0.5f;   // seconds the grace persists after leaving the radius

    float pushPosGain = 5.0f;         // corrective velocity per meter of position error (1/s)
    float pushRotGain = 5.0f;         // corrective angular velocity per radian of rotation error (1/s)
    float pushMaxVel = 10.0f;         // cap on the corrective (error-driven) velocity term (m/s)
    float pushMaxAngVel = 10.0f;      // cap on the corrective angular term (rad/s)
    float pushMaxAccel = 60.0f;       // how hard the push may change the body's velocity (m/s^2; keep > gravity)
    float pushMaxAngAccel = 60.0f;    // (rad/s^2)
    float pushCatchUpBoost = 4.0f;    // gain/cap multiplier in the catch-up band (snap..teleport threshold)
    float posTeleportThreshold = 10.0f; // beyond this the non-physical teleport resync fires after all
    // ARBITRATED OWNER (player-vs-player contact): the local feel comes from the local contact with
    // the opponent's replica — the server correction only reconciles REAL divergence (you actually
    // lost ground), so it runs with this much larger deadzone and halved gains; a tight deadzone
    // would micro-correct the pipeline lag and drag against the player's input for the whole window
    float arbitrateDeadzone = 0.4f;
};

export class NetworkManager
{
public:

    // Registers the "Network" tweak block; call once at startup regardless of role so the
    // section exists in single player too.
    void registerTweaks();

    bool startServer(uint16 port);
    // Accepts "ip[:port]" or "hostname[:port]" (blocking DNS); defaultPort fills a missing port.
    bool startClient(const std::string& address, uint16 defaultPort);
    void shutdown();

    ENetRole role() const { return m_role; }
    const NetSyncParams& params() const { return m_params; }

    // This process's stable clientId: server-minted per connection, delivered in the Welcome.
    // 0 on the server / before the Welcome — which is what makes ownerClientId == localClientId()
    // false everywhere except on the actual owner (plain aligned read, safe from job workers).
    uint32 localClientId() const { return m_localClientId; }

    // Client: this frame's positions of locally-owned dynamic bodies, rebuilt in receive() (main
    // thread, before the entity pass) — NetworkComponent::update reads them on job workers to arm
    // the interaction grace. Same publish-then-read-only contract as the snapshot targets.
    std::span<const glm::vec3> localOwnedBodyPositions() const { return m_localOwnedBodyPositions; }

    // Client: the server's snapshot rate from the Welcome (falls back to our own tweak) — the
    // remote-interpolation playback clock advances in the SERVER's tick units.
    float serverSnapshotHz() const { return m_serverSnapshotHz; }

    // Server: hand a networked entity (tree) to a client — its claims then drive it, validated by
    // the plausibility gate. Sets ownerClientId on every NetworkComponent in the tree and stamps the
    // spawn record so clients learn ownership with the Spawn message. Call right after spawning
    // (same frame, before send() announces); live ownership TRANSFER after announce is future work.
    void setOwner(Entity& root, uint32 clientId);

    // Server: fired on the main thread when a client completes the handshake / disconnects — the
    // App's hook for spawning and tearing down per-player entities. joined runs AFTER the Welcome
    // and world replay are queued, so anything spawned inside arrives in the same session stream.
    void setOnClientJoined(std::function<void(uint32 clientId)> callback) { m_onClientJoined = std::move(callback); }
    void setOnClientLeft(std::function<void(uint32 clientId)> callback) { m_onClientLeft = std::move(callback); }

    // Server: contact-driven ownership STEAL — the last player to collide with an object owns it,
    // even inside the previous owner's transfer bubble. Wired as the primaries' PhysicsComponent
    // onContact hook (main.cpp); steals from the server AND from other clients, but never another
    // client's PRIMARY (their player). Main thread (contact dispatch runs inside physics.update).
    void stealOwnershipOnContact(Entity& object, uint32 byClientId);

    void receive(double deltaSec); // main thread, before script/physics/world updates
    void send(double deltaSec);    // main thread, after world.update

    // Fires the named script event locally AND across the network (server -> all ready clients,
    // client -> server, which relays to the other clients). Role None = local fire only.
    // Callable from job workers (script thunks tick in the parallel entity pass): the network half
    // queues under a mutex and send() drains it on the main thread — NetHost is single-threaded.
    // Must be set BEFORE startServer/startClient — the transport reads it at open() and it cannot
    // change on a live host. Both ends must match or the handshake denies (a clean failure).
    static void setEncryption(bool enabled);

    void fireNetworkEvent(std::string_view name);

    // Who fired the event currently being dispatched: 0 = the server (or a local single-player
    // fire), else the originating client. Only meaningful INSIDE an event handler. Server-stamped
    // from the receiving peer, so a client cannot claim to be someone else.
    uint32 currentEventSender() const { return m_currentEventSender; }

    // NetworkComponent registration (main thread; spawns/destroys never run inside the parallel pass).
    // Returns the assigned netId — ids are a CODE abstraction, never authored in data, and only the
    // SERVER mints them: registration on the server (scene load or runtime alike) assigns the next id
    // and creates/extends the root's spawn record, which is what makes the entity appear on every
    // client; a client registering while executing a replicated Spawn takes the server's ids
    // (sequential in tree spawn order, deterministic DFS on both sides); any other registration
    // (client-local content, single player) returns 0 = LOCAL-INERT, kept out of the registry — no
    // conflict is possible because there is exactly one id authority. An occupied id is REPLACED
    // (with a warning): the Entity Editor's respawn creates the new entity while the old one is still
    // registered, so a collision there is the stale twin, not a duplicate — and unregister erases
    // only when the component pointer still matches, so the old entity's later destroy can't take the
    // new registration down with it. Unregistering a BASE id on the server queues the matching
    // Despawn (child ids of a partially-destroyed replicated tree are deliberately ignored — despawn
    // is all-or-nothing at the root).
    uint32 registerEntity(Entity& entity, NetworkComponent* comp);
    void unregisterEntity(uint32 netId, const NetworkComponent* comp);

    // "SERVER 2 peers | out 12.4 KB/s" — empty when role None or the stats tweak is off
    std::string getStatusText() const;

private:

    struct DynamicSpawn; // defined below with the registry members

    void sendHello();
    void handleSessionMessage(NetPeerId peer, NetReader& reader, uint8 msgType);
    void handleEventMessage(NetPeerId peer, std::span<const uint8> bytes);
    void handleSnapshot(NetReader& reader);
    void handleSpawnMessage(NetReader& reader);   // client: execute a replicated spawn with forced ids
    void handleDespawnMessage(NetReader& reader); // client: destroy the replicated root
    void handleClaimMessage(NetPeerId peer, NetReader& reader); // server: validate + apply an owner's claims
    void handleOwnerChangeMessage(NetReader& reader);           // client: adopt a per-entity ownership change
    void sendClaims();                            // client: stream claims for locally-owned entities
    // Server: per-entity ownership handover — resets the claim/validation state (the next claim
    // re-seeds against the twin) and broadcasts OwnerChange; 0 reverts to server-owned.
    void transferOwnership(uint32 netId, NetworkComponent* comp, uint32 newOwnerClientId);
    // Server, per frame: server-owned dynamics near a client's PRIMARY owned body transfer to that
    // client; transferred ones revert after "Release delay" outside "Release radius". Primaries only
    // as sources — chained transfer through a pushed cube would deadlock release on piles (the
    // client-side interaction grace covers second-order contact instead).
    void updateOwnershipTransfers(double deltaSec);
    void sendSnapshotTick();
    void sendEventTo(NetPeerId peer, std::string_view name, uint32 senderClientId);
    void fireEventAttributed(std::string_view name, uint32 senderClientId);
    void sendSpawnTo(NetPeerId peer, const DynamicSpawn& rec); // transform refreshed from the live entity when possible

    struct Replicated
    {
        Entity* entity = nullptr;
        NetworkComponent* comp = nullptr;
    };

    ENetRole m_role = ENetRole::None;
    NetHost m_host;
    NetPeerId m_serverPeer = InvalidNetPeerId; // client role: the one outgoing connection
    NetAddress m_serverAddress;                // client role: kept for auto-reconnect
    std::vector<NetPeerId> m_readyPeers;       // server role: peers past Hello (ids recycle — cleared on Disconnected)

    std::map<uint32, Replicated> m_entities;   // ordered: deterministic round-robin cursor
    mutable std::mutex m_entityMutex;

    // Server: one record per live replicated spawn — scene load and runtime alike (announced to
    // clients, replayed to late joiners; that replay is how the networked world arrives at connect).
    // Component ids are baseId..baseId+componentCount-1, contiguous because a tree spawn registers
    // synchronously on the main thread with nothing interleaving.
    struct DynamicSpawn
    {
        uint32 baseId = 0;
        uint32 componentCount = 0;
        uint32 ownerClientId = 0; // whole-tree ownership, applied by the client to every component of the spawn
        std::string path;      // what the client spawns (template sourceFile, prefab name fallback)
        glm::vec3 spawnPos = glm::vec3(0.0f);
        glm::quat spawnRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        float spawnScale = 1.0f;
    };
    std::map<uint32, DynamicSpawn> m_dynamicSpawns;              // by baseId (server)
    std::unordered_map<const Entity*, uint32> m_dynamicRootIds;  // root -> baseId, grouping WITHIN one spawn call; purged every send() (entity pointers may recycle)
    std::vector<uint32> m_pendingSpawnAnnounce;                  // announced by send(); client duplicate-guard makes replay+announce overlap harmless
    std::vector<uint32> m_pendingDespawn;
    uint32 m_nextNetId = 1;    // server-minted, the only id authority; 0 = local-inert (never registered)

    // Client: id assignment scope while executing one replicated Spawn (main thread, synchronous)
    uint32 m_incomingSpawnBase = 0;
    uint32 m_incomingSpawnCount = 0;
    uint32 m_incomingSpawnCursor = 0;
    uint32 m_incomingSpawnOwner = 0;

    // clientIds: stable per connection, minted by the server at Hello (peer ids recycle, clientIds don't)
    uint32 m_localClientId = 0; // client: from the Welcome; always 0 on the server
    uint32 m_currentEventSender = 0; // see currentEventSender()
    uint32 m_nextClientId = 1;  // server
    std::unordered_map<NetPeerId, uint32> m_peerClients; // server: ready peer -> clientId (erased on Disconnected)
    std::function<void(uint32 clientId)> m_onClientJoined; // server, main thread
    std::function<void(uint32 clientId)> m_onClientLeft;

    // Client: recent claim payloads per locally-owned entity — every Claim packet carries the whole
    // ring, so a claim only vanishes if ClaimRedundancy consecutive packets drop
    struct ClaimRecord
    {
        NetInputState input;
        glm::vec3 pos;
        glm::quat rot;
        glm::vec3 linVel;
        glm::vec3 angVel;
    };
    static constexpr uint32 ClaimRedundancy = 8;
    struct ClaimRing
    {
        ClaimRecord records[ClaimRedundancy]; // indexed seq % ClaimRedundancy
        uint32 newestSeq = 0;
        // valid consecutive records ending at newestSeq — the packet's record count comes from THIS,
        // never from the (monotonic, episode-spanning) claim sequence: a re-acquired entity's first
        // packet must not replay the previous episode's stale positions (the server's dedup is reset
        // on ownership change, so it would apply them — teleporting the twin to where the object WAS)
        uint32 validCount = 0;
    };
    std::unordered_map<uint32, ClaimRing> m_claimRings; // by netId, created on demand for owned entities
    // claims are phase-locked to the physics step (see send()): the step counter they last fired on,
    // and the wall clock, which only serves the "Max update Hz" thinning limit
    uint32 m_lastClaimStep = 0;
    double m_lastClaimTime = 0.0;
    std::vector<glm::vec3> m_localOwnedBodyPositions;   // see localOwnedBodyPositions()
    float m_serverSnapshotHz = 20.0f;                   // see serverSnapshotHz()
    // remote-owned entities' snapshot history for interpolation playback: unique_ptr values keep the
    // rings' addresses stable (components hold raw pointers across map growth)
    std::map<uint32, std::unique_ptr<NetSnapshotRing>> m_remoteBuffers;
    std::vector<std::pair<uint32, glm::vec3>> m_transferSources; // server scan scratch: clientId + primary body position

    std::vector<std::string> m_pendingOutgoingEvents; // filled from any thread, drained by send()
    std::mutex m_eventMutex;

    NetSyncParams m_params;
    uint32 m_roundRobinCursor = 0;
    double m_snapshotAccum = 0.0;
    double m_netTime = 0.0; // seconds since start, advanced in receive() — the WALL CLOCK the claim
                            // displacement budget is measured against (sequence numbers are
                            // attacker-controlled, so they can only ever narrow the budget)
    uint32 m_serverTick = 0;
    bool m_warnedUnknownRecFlags = false;
    // no unknown-id warning list: an unknown snapshot id is always a Spawn in flight or a Despawn
    // that already landed — expected transients, the reliable spawn stream is the source of truth
};

export namespace Globals
{
    NetworkManager networkManager;
}
