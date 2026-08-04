export module Entity:NetworkManager;

import Core;
import Core.glm;
import Network;
import :Entity;

// Server-authoritative entity sync over the Network library's reliable-UDP NetHost. One manager per
// process, role picked at startup: Server opens a listening host, Client connects out, None (single
// player) leaves everything inert. NetworkComponents register themselves here by netId; the server
// streams local pos/rot snapshots for registered entities at "Network/Snapshot Hz" and clients
// blend/snap their copies toward the received targets (NetworkComponent::update). Named script events
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
//   ch1 Reliable:    Event [u8][string name]
//   ch2 Reliable:    Hello (client->server) [u8][u16 version][varuint count][u32 checksum]
//                    Welcome (server->client) [u8][u16 version][varuint serverTick][f32 snapshotHz][varuint count][u32 checksum]
//                    Deny [u8][string reason]
// The netId count/checksum exchange detects scene mismatch (both sides load the same scene this
// milestone — no spawn replication yet); mismatch warns and proceeds, the server stays authoritative.

export struct NetworkComponent;

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
    float posSnapThreshold = 2.0f;    // above: hard snap to target
    float rotDeadzoneDeg = 0.5f;
    float rotSnapThresholdDeg = 45.0f;
    float blendRate = 10.0f;          // exponential blend rate between deadzone and snap
    bool syncVelocities = true;       // apply the server's velocities once per received snapshot
    bool extrapolate = true;          // dead-reckon the pose target by linVel * timeSinceSnapshot
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
    bool isAuthority() const { return m_role != ENetRole::Client; }
    const NetSyncParams& params() const { return m_params; }

    void receive(double deltaSec); // main thread, before script/physics/world updates
    void send(double deltaSec);    // main thread, after world.update

    // Fires the named script event locally AND across the network (server -> all ready clients,
    // client -> server, which relays to the other clients). Role None = local fire only.
    // Callable from job workers (script thunks tick in the parallel entity pass): the network half
    // queues under a mutex and send() drains it on the main thread — NetHost is single-threaded.
    void fireNetworkEvent(std::string_view name);

    // NetworkComponent registration (main thread; spawns/destroys never run inside the parallel pass).
    // An occupied id is REPLACED (with a warning): the Entity Editor's respawn creates the new entity
    // while the old one is still registered, so a collision there is the stale twin, not a duplicate —
    // and unregister erases only when the component pointer still matches, so the old entity's later
    // destroy can't take the new registration down with it.
    void registerEntity(uint32 netId, Entity* entity, NetworkComponent* comp);
    void unregisterEntity(uint32 netId, const NetworkComponent* comp);
    // fnv1a over the root-to-entity name path, probed until free — deterministic across machines
    // because spawn order is (same scene, main-thread spawning).
    uint32 deriveAutoId(const Entity& entity);

    // "SERVER 2 peers | out 12.4 KB/s" — empty when role None or the stats tweak is off
    std::string getStatusText() const;

private:

    void sendHello();
    void handleSessionMessage(NetPeerId peer, NetReader& reader, uint8 msgType);
    void handleEventMessage(NetPeerId peer, std::span<const uint8> bytes);
    void handleSnapshot(NetReader& reader);
    void sendSnapshotTick();
    void sendEventTo(NetPeerId peer, std::string_view name);
    uint32 registryChecksum() const; // XOR of netId * 2654435761u over the registry

    struct Replicated
    {
        Entity* entity = nullptr;
        NetworkComponent* comp = nullptr;
    };

    ENetRole m_role = ENetRole::None;
    NetHost m_host;
    NetPeerId m_serverPeer = InvalidNetPeerId; // client role: the one outgoing connection
    std::vector<NetPeerId> m_readyPeers;       // server role: peers past Hello (ids recycle — cleared on Disconnected)

    std::map<uint32, Replicated> m_entities;   // ordered: deterministic round-robin cursor
    mutable std::mutex m_entityMutex;

    std::vector<std::string> m_pendingOutgoingEvents; // filled from any thread, drained by send()
    std::mutex m_eventMutex;

    NetSyncParams m_params;
    uint32 m_roundRobinCursor = 0;
    double m_snapshotAccum = 0.0;
    uint32 m_serverTick = 0;
    bool m_warnedUnknownRecFlags = false;
    std::vector<uint32> m_warnedUnknownIds;    // client: log each unknown incoming netId once
};

export namespace Globals
{
    NetworkManager networkManager;
}
