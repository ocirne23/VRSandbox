module Entity;

import Core;
import Core.Log;
import Core.glm;
import Core.Tweaks;
import Network;
import Physics;

// Bump on ANY wire format change: the transport handshake denies mismatched protocol ids, so old
// builds fail to connect instead of misparsing. GameNetVersion rides in Hello/Welcome purely so the
// mismatch produces a readable log line when the protocolId was forgotten.
constexpr uint32 GameProtocolId = 0x56525333; // "VRS3": physics records (velocities + quantization ranges)
constexpr uint16 GameNetVersion = 2;

enum class ENetMsg : uint8
{
    Hello = 1, // client -> server, ch2
    Welcome,   // server -> client, ch2
    Deny,      // server -> client, ch2
    Snapshot,  // server -> client, ch0
    Event,     // both directions, ch1
};

constexpr uint8 SnapshotFlag_Quantized = 1 << 0;
constexpr uint8 ChannelSnapshot = 0;
constexpr uint8 ChannelEvent = 1;
constexpr uint8 ChannelSession = 2;

// ---- tweaks (server send policy; correction thresholds live in NetSyncParams) ------------------
static float s_snapshotHz = 20.0f;       // matches the physics fixed step
static bool  s_quantize = true;
static int   s_snapshotMaxBytes = 1100;  // stay under the transport's unreliable drop ceiling (~1175)
static int   s_maxEntitiesPerTick = 200;
static int   s_keyframeEveryTicks = 20;  // unmoved entities refresh on this rotation (drift/late-join repair)
static float s_sendPosEpsilon = 0.001f;
static float s_sendRotEpsilonDeg = 0.1f;
static float s_maxVel = 50.0f;    // velocity quantization range (m/s); sent per message so both ends agree
static float s_maxAngVel = 50.0f; // angular velocity quantization range (rad/s)
static bool  s_showStats = true;

static uint32 fnv1a32(const void* data, size_t numBytes, uint32 hash = 2166136261u)
{
    const uint8* bytes = static_cast<const uint8*>(data);
    for (size_t i = 0; i < numBytes; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static float quatAngleDeg(const glm::quat& a, const glm::quat& b)
{
    const float d = glm::min(1.0f, glm::abs(glm::dot(a, b)));
    return glm::degrees(2.0f * glm::acos(d));
}

// Smallest-three quaternion packing: 2-bit largest-component index + 3x10-bit snorm over
// [-1/sqrt2, 1/sqrt2] (the largest component is >= the others, so the rest fit that range).
static uint32 packQuat(const glm::quat& q)
{
    const float comps[4] = { q.x, q.y, q.z, q.w };
    uint32 largest = 0;
    for (uint32 i = 1; i < 4; ++i)
        if (glm::abs(comps[i]) > glm::abs(comps[largest]))
            largest = i;
    const float sign = comps[largest] < 0.0f ? -1.0f : 1.0f; // q and -q are the same rotation
    constexpr float invSqrt2 = 0.70710678f;
    uint32 packed = largest << 30;
    uint32 shift = 0;
    for (uint32 i = 0; i < 4; ++i)
    {
        if (i == largest)
            continue;
        const float v = glm::clamp(comps[i] * sign / invSqrt2, -1.0f, 1.0f);
        packed |= uint32((v * 0.5f + 0.5f) * 1023.0f + 0.5f) << (shift * 10);
        ++shift;
    }
    return packed;
}

static glm::quat unpackQuat(uint32 packed)
{
    const uint32 largest = packed >> 30;
    constexpr float invSqrt2 = 0.70710678f;
    float comps[4];
    float sumSq = 0.0f;
    uint32 shift = 0;
    for (uint32 i = 0; i < 4; ++i)
    {
        if (i == largest)
            continue;
        const float v = (float((packed >> (shift * 10)) & 1023u) / 1023.0f * 2.0f - 1.0f) * invSqrt2;
        comps[i] = v;
        sumSq += v * v;
        ++shift;
    }
    comps[largest] = glm::sqrt(glm::max(0.0f, 1.0f - sumSq));
    return glm::normalize(glm::quat(comps[3], comps[0], comps[1], comps[2]));
}

static const char* disconnectReasonName(ENetDisconnectReason reason)
{
    switch (reason)
    {
    case ENetDisconnectReason::Local:         return "local";
    case ENetDisconnectReason::Remote:        return "remote";
    case ENetDisconnectReason::Timeout:       return "timeout";
    case ENetDisconnectReason::ConnectFailed: return "connect failed";
    case ENetDisconnectReason::Denied:        return "denied";
    default:                                  return "none";
    }
}

void NetworkManager::registerTweaks()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    s_registered = true;

    Tweak::floatVar("Network", "Snapshot Hz", &s_snapshotHz, 1.0f, 60.0f, 0.5f);
    Tweak::boolean("Network", "Quantize", &s_quantize);
    Tweak::intVar("Network", "Snapshot max bytes", &s_snapshotMaxBytes, 128, 1400);
    Tweak::intVar("Network", "Max entities per tick", &s_maxEntitiesPerTick, 1, 4096);
    Tweak::intVar("Network", "Keyframe every ticks", &s_keyframeEveryTicks, 1, 255);
    Tweak::floatVar("Network", "Send pos epsilon", &s_sendPosEpsilon, 0.0f, 0.1f, 0.0005f);
    Tweak::floatVar("Network", "Send rot epsilon (deg)", &s_sendRotEpsilonDeg, 0.0f, 10.0f, 0.01f);
    Tweak::floatVar("Network", "Max vel (quantize m/s)", &s_maxVel, 1.0f, 500.0f, 1.0f);
    Tweak::floatVar("Network", "Max ang vel (quantize rad/s)", &s_maxAngVel, 1.0f, 200.0f, 1.0f);
    Tweak::boolean("Network", "Show stats", &s_showStats);

    Tweak::floatVar("Network/Correction", "Pos deadzone", &m_params.posDeadzone, 0.0f, 1.0f, 0.005f);
    Tweak::floatVar("Network/Correction", "Pos snap threshold", &m_params.posSnapThreshold, 0.0f, 10.0f, 0.05f);
    Tweak::floatVar("Network/Correction", "Rot deadzone (deg)", &m_params.rotDeadzoneDeg, 0.0f, 30.0f, 0.1f);
    Tweak::floatVar("Network/Correction", "Rot snap threshold (deg)", &m_params.rotSnapThresholdDeg, 0.0f, 180.0f, 0.5f);
    Tweak::floatVar("Network/Correction", "Blend rate", &m_params.blendRate, 0.0f, 30.0f, 0.1f);
    Tweak::boolean("Network/Correction", "Sync velocities", &m_params.syncVelocities);
    Tweak::boolean("Network/Correction", "Extrapolate", &m_params.extrapolate);

    // live-editable transport link simulation (outgoing packets)
    NetHostConfig& config = m_host.config();
    Tweak::floatVar("Network/Link sim", "Packet loss", &config.simPacketLoss, 0.0f, 1.0f, 0.005f);
    Tweak::floatVar("Network/Link sim", "Latency (ms)", &config.simLatencyMs, 0.0f, 1000.0f, 1.0f);
    Tweak::floatVar("Network/Link sim", "Jitter (ms)", &config.simJitterMs, 0.0f, 500.0f, 1.0f);
}

bool NetworkManager::startServer(uint16 port)
{
    assert(m_role == ENetRole::None);
    registerTweaks();
    NetHostConfig config;
    config.protocolId = GameProtocolId;
    config.acceptIncoming = true;
    if (!m_host.open(port, config))
    {
        Log::error("Network: failed to open server port " + std::to_string(port));
        return false;
    }
    m_role = ENetRole::Server;
    Log::info("Network: SERVER listening on port " + std::to_string(m_host.getLocalPort()));
    return true;
}

bool NetworkManager::startClient(const std::string& address, uint16 defaultPort)
{
    assert(m_role == ENetRole::None);
    registerTweaks();
    NetAddress addr = NetAddress::fromString(address);
    if (!addr.isValid())
    {
        // not a dotted quad: treat as hostname[:port]
        const size_t colon = address.rfind(':');
        std::string host = address;
        uint16 port = defaultPort;
        if (colon != std::string::npos)
        {
            host = address.substr(0, colon);
            port = uint16(std::atoi(address.c_str() + colon + 1));
        }
        addr = netResolveHost(host, port);
    }
    if (addr.port == 0)
        addr.port = defaultPort;
    if (!addr.isValid())
    {
        Log::error("Network: cannot resolve server address '" + address + "'");
        return false;
    }
    NetHostConfig config;
    config.protocolId = GameProtocolId;
    config.acceptIncoming = false;
    if (!m_host.open(0, config))
    {
        Log::error("Network: failed to open client socket");
        return false;
    }
    m_serverPeer = m_host.connect(addr);
    m_role = ENetRole::Client;
    Log::info("Network: CLIENT connecting to " + addr.toString());
    return true;
}

void NetworkManager::shutdown()
{
    m_host.close();
    m_role = ENetRole::None;
    m_serverPeer = InvalidNetPeerId;
    m_readyPeers.clear();
}

void NetworkManager::receive(double deltaSec)
{
    if (m_role == ENetRole::None)
        return;
    ProfileScope scope("NetworkManager::receive", EProfileCategory::Network);

    m_host.update(deltaSec);
    for (NetEvent& evt : m_host.takeEvents())
    {
        switch (evt.type)
        {
        case ENetEventType::Connected:
            if (m_role == ENetRole::Client)
            {
                Log::info("Network: connected to server " + m_host.getPeerAddress(evt.peer).toString());
                sendHello();
            }
            else
                Log::info("Network: client connected from " + m_host.getPeerAddress(evt.peer).toString());
            break;

        case ENetEventType::Disconnected:
            // NetPeerIds recycle: drop every bit of per-peer state the moment the peer dies
            std::erase(m_readyPeers, evt.peer);
            if (m_role == ENetRole::Client && evt.peer == m_serverPeer)
            {
                Log::warning(std::string("Network: disconnected from server (") + disconnectReasonName(evt.reason) + ")");
                m_serverPeer = InvalidNetPeerId;
            }
            else if (m_role == ENetRole::Server)
                Log::info(std::string("Network: client disconnected (") + disconnectReasonName(evt.reason) + ")");
            break;

        case ENetEventType::Message:
        {
            if (evt.data.empty())
                break;
            const uint8 msgType = evt.data[0];
            NetReader reader(std::span<const uint8>(evt.data).subspan(1));
            switch (ENetMsg(msgType))
            {
            case ENetMsg::Hello:
            case ENetMsg::Welcome:
            case ENetMsg::Deny:
                handleSessionMessage(evt.peer, reader, msgType);
                break;
            case ENetMsg::Snapshot:
                if (m_role == ENetRole::Client)
                    handleSnapshot(reader);
                break;
            case ENetMsg::Event:
                handleEventMessage(evt.peer, evt.data);
                break;
            default:
                Log::warning("Network: unknown message type " + std::to_string(msgType));
                break;
            }
            break;
        }
        }
    }
}

void NetworkManager::sendHello()
{
    uint8 buffer[64];
    NetWriter writer(buffer);
    writer.write<uint8>(uint8(ENetMsg::Hello));
    writer.write<uint16>(GameNetVersion);
    {
        const std::lock_guard<std::mutex> lock(m_entityMutex);
        writer.writeVarUInt(m_entities.size());
        writer.write<uint32>(registryChecksum());
    }
    assert(!writer.overflowed());
    m_host.send(m_serverPeer, writer.data(), ENetDelivery::Reliable, ChannelSession);
}

void NetworkManager::handleSessionMessage(NetPeerId peer, NetReader& reader, uint8 msgType)
{
    switch (ENetMsg(msgType))
    {
    case ENetMsg::Hello:
    {
        if (m_role != ENetRole::Server)
            break;
        const uint16 version = reader.read<uint16>();
        const uint64 count = reader.readVarUInt();
        const uint32 checksum = reader.read<uint32>();
        if (reader.overflowed())
            break;
        if (version != GameNetVersion)
        {
            Log::warning("Network: denying client with version " + std::to_string(version) + " (ours " + std::to_string(GameNetVersion) + ")");
            uint8 buffer[128];
            NetWriter writer(buffer);
            writer.write<uint8>(uint8(ENetMsg::Deny));
            writer.writeString("version mismatch");
            m_host.send(peer, writer.data(), ENetDelivery::Reliable, ChannelSession);
            m_host.disconnect(peer);
            break;
        }
        uint64 localCount;
        uint32 localChecksum;
        {
            const std::lock_guard<std::mutex> lock(m_entityMutex);
            localCount = m_entities.size();
            localChecksum = registryChecksum();
        }
        if (count != localCount || checksum != localChecksum)
            Log::warning("Network: client scene mismatch (client " + std::to_string(count) + " networked entities, server "
                + std::to_string(localCount) + ") - server stays authoritative, unmatched entities won't sync");
        if (std::find(m_readyPeers.begin(), m_readyPeers.end(), peer) == m_readyPeers.end())
            m_readyPeers.push_back(peer);
        uint8 buffer[64];
        NetWriter writer(buffer);
        writer.write<uint8>(uint8(ENetMsg::Welcome));
        writer.write<uint16>(GameNetVersion);
        writer.writeVarUInt(m_serverTick);
        writer.write<float>(s_snapshotHz);
        writer.writeVarUInt(localCount);
        writer.write<uint32>(localChecksum);
        assert(!writer.overflowed());
        m_host.send(peer, writer.data(), ENetDelivery::Reliable, ChannelSession);
        Log::info("Network: client ready (" + std::to_string(m_readyPeers.size()) + " total)");
        break;
    }
    case ENetMsg::Welcome:
    {
        if (m_role != ENetRole::Client)
            break;
        const uint16 version = reader.read<uint16>();
        const uint64 serverTick = reader.readVarUInt();
        const float snapshotHz = reader.read<float>();
        const uint64 count = reader.readVarUInt();
        const uint32 checksum = reader.read<uint32>();
        if (reader.overflowed())
            break;
        uint64 localCount;
        uint32 localChecksum;
        {
            const std::lock_guard<std::mutex> lock(m_entityMutex);
            localCount = m_entities.size();
            localChecksum = registryChecksum();
        }
        if (count != localCount || checksum != localChecksum)
            Log::warning("Network: scene mismatch with server (server " + std::to_string(count) + " networked entities, ours "
                + std::to_string(localCount) + ") - unmatched entities won't sync");
        Log::info("Network: welcomed by server (version " + std::to_string(version) + ", " + std::to_string(count)
            + " entities, " + std::to_string(snapshotHz) + " Hz, tick " + std::to_string(serverTick) + ")");
        break;
    }
    case ENetMsg::Deny:
    {
        const std::string_view reason = reader.readString();
        Log::warning("Network: server denied us: " + std::string(reason));
        break;
    }
    default:
        break;
    }
}

void NetworkManager::handleEventMessage(NetPeerId peer, std::span<const uint8> bytes)
{
    NetReader reader(bytes.subspan(1));
    const std::string_view name = reader.readString();
    if (reader.overflowed() || name.empty())
        return;
    Log::info("Network: event '" + std::string(name) + "' received");
    Globals::scriptEvents.fireEvent(std::string(name));
    if (m_role == ENetRole::Server) // relay to every other ready client, bytes forwarded as-is
        for (const NetPeerId other : m_readyPeers)
            if (other != peer)
                m_host.send(other, bytes, ENetDelivery::Reliable, ChannelEvent);
}

void NetworkManager::handleSnapshot(NetReader& reader)
{
    const uint32 tick = uint32(reader.readVarUInt());
    const uint8 flags = reader.read<uint8>();
    if (flags & ~SnapshotFlag_Quantized)
    {
        // a future format we don't know how to parse — a per-record size is not recoverable, drop the message
        if (!m_warnedUnknownRecFlags)
        {
            m_warnedUnknownRecFlags = true;
            Log::warning("Network: snapshot with unknown flags " + std::to_string(flags) + ", dropping (newer server?)");
        }
        return;
    }
    const bool quantized = (flags & SnapshotFlag_Quantized) != 0;
    float maxVel = 50.0f;
    float maxAngVel = 50.0f;
    if (quantized) // the quantization ranges are live tweaks server-side, so they ride in the header
    {
        maxVel = reader.read<float>();
        maxAngVel = reader.read<float>();
    }
    const uint32 count = reader.read<uint16>();

    const std::lock_guard<std::mutex> lock(m_entityMutex);
    for (uint32 i = 0; i < count; ++i)
    {
        const uint32 netId = uint32(reader.readVarUInt());
        const uint8 recFlags = reader.read<uint8>();
        if (recFlags & ~(NetRecFlag_Physics | NetRecFlag_Asleep))
        {
            if (!m_warnedUnknownRecFlags)
            {
                m_warnedUnknownRecFlags = true;
                Log::warning("Network: snapshot record with unknown flags " + std::to_string(recFlags) + ", dropping message (newer server?)");
            }
            return; // unknown bits = unknown record size, the rest of the message is unparseable
        }
        const glm::vec3 pos = reader.read<glm::vec3>();
        const glm::quat rot = quantized ? unpackQuat(reader.read<uint32>()) : reader.read<glm::quat>();
        glm::vec3 linVel(0.0f);
        glm::vec3 angVel(0.0f);
        if (recFlags & NetRecFlag_Physics)
        {
            if (quantized)
            {
                for (int c = 0; c < 3; ++c) linVel[c] = reader.readQuantized<uint16>(-maxVel, maxVel);
                for (int c = 0; c < 3; ++c) angVel[c] = reader.readQuantized<uint16>(-maxAngVel, maxAngVel);
            }
            else
            {
                linVel = reader.read<glm::vec3>();
                angVel = reader.read<glm::vec3>();
            }
        }
        if (reader.overflowed())
            return;

        const auto it = m_entities.find(netId);
        if (it == m_entities.end())
        {
            if (std::find(m_warnedUnknownIds.begin(), m_warnedUnknownIds.end(), netId) == m_warnedUnknownIds.end())
            {
                m_warnedUnknownIds.push_back(netId);
                Log::warning("Network: snapshot for unknown netId " + std::to_string(netId) + " (scene mismatch?)");
            }
            continue;
        }
        NetworkComponent* comp = it->second.comp;
        if (comp->hasTarget && tick <= comp->serverTick)
            continue; // stale (reordered unreliable packet)
        comp->targetPos = pos;
        comp->targetRot = rot;
        comp->targetLinVel = linVel;
        comp->targetAngVel = angVel;
        comp->targetFlags = recFlags;
        comp->serverTick = tick;
        comp->timeSinceSnapshot = 0.0f;
        comp->hasTarget = true;
    }
}

void NetworkManager::send(double deltaSec)
{
    if (m_role == ENetRole::None)
        return;
    ProfileScope scope("NetworkManager::send", EProfileCategory::Network);

    // outgoing events queued this frame (worker-thread script thunks can't touch NetHost themselves)
    std::vector<std::string> pendingEvents;
    {
        const std::lock_guard<std::mutex> lock(m_eventMutex);
        pendingEvents.swap(m_pendingOutgoingEvents);
    }
    for (const std::string& name : pendingEvents)
    {
        if (m_role == ENetRole::Server)
        {
            for (const NetPeerId peer : m_readyPeers)
                sendEventTo(peer, name);
        }
        else if (m_serverPeer != InvalidNetPeerId && m_host.isConnected(m_serverPeer))
            sendEventTo(m_serverPeer, name); // the server re-fires locally and relays to other clients
    }

    if (m_role == ENetRole::Server)
    {
        const double interval = 1.0 / double(glm::clamp(s_snapshotHz, 1.0f, 240.0f));
        m_snapshotAccum = glm::min(m_snapshotAccum + deltaSec, interval * 4.0); // don't spiral after a hitch
        while (m_snapshotAccum >= interval)
        {
            m_snapshotAccum -= interval;
            ++m_serverTick;
            if (m_host.getConnectedCount() > 0)
                sendSnapshotTick();
        }
    }
    m_host.update(0.0); // flush everything queued this frame (send() only queues until the next update)
}

void NetworkManager::sendSnapshotTick()
{
    uint8 buffer[1400];
    const size_t capacity = size_t(glm::clamp(s_snapshotMaxBytes, 128, 1400));
    NetWriter writer(std::span<uint8>(buffer, capacity));
    size_t countOffset = 0;
    uint16 count = 0;

    const float maxVel = glm::max(1.0f, s_maxVel);
    const float maxAngVel = glm::max(1.0f, s_maxAngVel);
    const auto beginMessage = [&]
    {
        writer.reset();
        writer.write<uint8>(uint8(ENetMsg::Snapshot));
        writer.writeVarUInt(m_serverTick);
        writer.write<uint8>(s_quantize ? SnapshotFlag_Quantized : 0);
        if (s_quantize) // decode needs the (live-tweakable) velocity quantization ranges
        {
            writer.write<float>(maxVel);
            writer.write<float>(maxAngVel);
        }
        countOffset = writer.size();
        writer.write<uint16>(0); // patched by flushMessage
        count = 0;
    };
    const auto flushMessage = [&]
    {
        if (count == 0)
            return;
        writer.writeAt(countOffset, count);
        assert(!writer.overflowed());
        m_host.sendToAll(writer.data(), ENetDelivery::Unreliable, ChannelSnapshot);
    };

    const std::lock_guard<std::mutex> lock(m_entityMutex);
    if (m_entities.empty())
        return;
    beginMessage();

    const uint32 keyframeTicks = uint32(glm::max(1, s_keyframeEveryTicks));
    const float posEpsilonSq = s_sendPosEpsilon * s_sendPosEpsilon;
    auto it = m_entities.lower_bound(m_roundRobinCursor);
    if (it == m_entities.end())
        it = m_entities.begin();
    const size_t total = m_entities.size();
    int sent = 0;
    for (size_t visited = 0; visited < total && sent < s_maxEntitiesPerTick; ++visited)
    {
        const uint32 netId = it->first;
        Entity* entity = it->second.entity;
        NetworkComponent* comp = it->second.comp;
        ++it;
        if (it == m_entities.end())
            it = m_entities.begin();

        const bool keyframe = (m_serverTick % keyframeTicks) == (netId % keyframeTicks);
        const PhysicsComponent* physics = getComponent<PhysicsComponent>(entity);
        const bool physicsBody = physics && physics->bodyType == EPhysicsBodyType::Dynamic
            && physics->body.isValid() && !physics->suspended && physics->enabled;

        glm::vec3 pos;
        glm::quat rot;
        uint8 recFlags = 0;
        glm::vec3 linVel(0.0f);
        glm::vec3 angVel(0.0f);
        if (physicsBody)
        {
            // the BODY's world pose is authoritative (and what the client teleports its body to);
            // awake bodies send every tick, then ONE final record on the awake->asleep edge so the
            // client hard-syncs and sleeps too — after that only the keyframe rotation refreshes
            const bool asleep = !physics->body.isAwake();
            if (!asleep)
                comp->sleepDirty = true;
            else if (comp->sleepDirty)
                comp->sleepDirty = false;
            else if (!keyframe)
                continue;
            recFlags = NetRecFlag_Physics | (asleep ? NetRecFlag_Asleep : 0);
            pos = physics->body.getPosition();
            rot = physics->body.getRotation();
            linVel = physics->body.getLinearVelocity();
            angVel = physics->body.getAngularVelocity();
        }
        else
        {
            // entity-LOCAL transform: moved-since-last-send + the keyframe rotation for convergence
            const glm::vec3 posDelta = entity->pos - comp->lastSentPos;
            const bool moved = glm::dot(posDelta, posDelta) > posEpsilonSq
                || quatAngleDeg(entity->rot, comp->lastSentRot) > s_sendRotEpsilonDeg;
            if (!keyframe && !moved)
                continue;
            pos = entity->pos;
            rot = entity->rot;
            comp->lastSentPos = pos;
            comp->lastSentRot = rot;
        }

        constexpr size_t MaxRecordBytes = 5 + 1 + 12 + 16 + 24; // varint id + flags + pos + raw quat + raw velocities
        if (writer.size() + MaxRecordBytes > writer.capacity())
        {
            flushMessage();
            beginMessage();
        }
        writer.writeVarUInt(netId);
        writer.write<uint8>(recFlags);
        writer.write(pos);
        if (s_quantize)
            writer.write<uint32>(packQuat(rot));
        else
            writer.write(rot);
        if (recFlags & NetRecFlag_Physics)
        {
            if (s_quantize)
            {
                for (int c = 0; c < 3; ++c) writer.writeQuantized<uint16>(linVel[c], -maxVel, maxVel);
                for (int c = 0; c < 3; ++c) writer.writeQuantized<uint16>(angVel[c], -maxAngVel, maxAngVel);
            }
            else
            {
                writer.write(linVel);
                writer.write(angVel);
            }
        }
        ++count;
        ++sent;
    }
    m_roundRobinCursor = it != m_entities.end() ? it->first : 0;
    flushMessage();
}

void NetworkManager::fireNetworkEvent(std::string_view name)
{
    if (name.empty())
        return;
    Globals::scriptEvents.fireEvent(std::string(name)); // same worker-safety contract as thunk_sendEvent
    if (m_role == ENetRole::None)
        return;
    // NetHost is single-threaded and this is reachable from worker-thread script thunks during the
    // parallel entity pass — queue, send() drains on the main thread the same frame
    const std::lock_guard<std::mutex> lock(m_eventMutex);
    m_pendingOutgoingEvents.emplace_back(name);
}

void NetworkManager::sendEventTo(NetPeerId peer, std::string_view name)
{
    uint8 buffer[512];
    NetWriter writer(buffer);
    writer.write<uint8>(uint8(ENetMsg::Event));
    writer.writeString(name);
    if (writer.overflowed())
    {
        Log::warning("Network: event name too long, not sent: " + std::string(name.substr(0, 64)));
        return;
    }
    m_host.send(peer, writer.data(), ENetDelivery::Reliable, ChannelEvent);
}

void NetworkManager::registerEntity(uint32 netId, Entity* entity, NetworkComponent* comp)
{
    const std::lock_guard<std::mutex> lock(m_entityMutex);
    const auto [it, inserted] = m_entities.insert_or_assign(netId, Replicated{ entity, comp });
    if (!inserted)
        // usually the Entity Editor respawning this entity (new spawns before old dies); a genuine
        // duplicate authored id also lands here, and only the newest twin syncs — assign unique ids
        Log::warning("Network: netId " + std::to_string(netId) + " re-registered by entity '"
            + entity->getName() + "' (editor respawn, or a duplicate authored Id)");
}

void NetworkManager::unregisterEntity(uint32 netId, const NetworkComponent* comp)
{
    const std::lock_guard<std::mutex> lock(m_entityMutex);
    const auto it = m_entities.find(netId);
    if (it != m_entities.end() && it->second.comp == comp) // a replaced (stale) twin must not erase its successor
        m_entities.erase(it);
}

uint32 NetworkManager::deriveAutoId(const Entity& entity)
{
    // hash the root-to-entity name path: stable across runs as long as both sides load the same scene
    const Entity* chain[32];
    uint32 depth = 0;
    for (const Entity* e = &entity; e && depth < 32; e = e->parent)
        chain[depth++] = e;
    uint32 hash = 2166136261u;
    for (uint32 i = depth; i-- > 0;)
    {
        const char* name = chain[i]->getName();
        hash = fnv1a32(name, strlen(name), hash);
        hash = fnv1a32("/", 1, hash);
    }
    hash &= 0x7fffffffu; // bit 31 reserved for future server-assigned dynamic-spawn ids
    hash = glm::max(1u, hash);
    // same-name siblings (three "cube" prefab children) hash identically: probe until free. Spawn order
    // is deterministic (same scene, main-thread spawning), so both sides probe to the same ids.
    const std::lock_guard<std::mutex> lock(m_entityMutex);
    while (m_entities.contains(hash))
        hash = glm::max(1u, fnv1a32(&hash, sizeof(hash)) & 0x7fffffffu);
    return hash;
}

uint32 NetworkManager::registryChecksum() const
{
    uint32 checksum = 0;
    for (const auto& [netId, replicated] : m_entities)
        checksum ^= netId * 2654435761u;
    return checksum;
}

std::string NetworkManager::getStatusText() const
{
    if (m_role == ENetRole::None || !s_showStats)
        return {};
    const NetHostStats& stats = m_host.getStats();
    const auto kbs = [](uint32 bytesPerSec) { return std::to_string((bytesPerSec * 10) / 1024 / 10) + "." + std::to_string((bytesPerSec * 10 / 1024) % 10); };
    if (m_role == ENetRole::Server)
        return "SERVER " + std::to_string(m_host.getConnectedCount()) + " peers | out " + kbs(stats.bytesSentPerSec)
            + " KB/s in " + kbs(stats.bytesReceivedPerSec) + " KB/s";
    if (m_serverPeer == InvalidNetPeerId || !m_host.isConnected(m_serverPeer))
        return "CLIENT connecting...";
    return "CLIENT rtt " + std::to_string(int(m_host.getPeerRttMs(m_serverPeer))) + " ms loss "
        + std::to_string(int(m_host.getPeerPacketLoss(m_serverPeer) * 100.0f)) + "% | in " + kbs(stats.bytesReceivedPerSec)
        + " KB/s out " + kbs(stats.bytesSentPerSec) + " KB/s";
}
