module Entity;

import Core;
import Core.Log;
import Core.glm;
import Core.Transform;
import Physics;

static float quatAngleDeg(const glm::quat& a, const glm::quat& b)
{
    const float d = glm::min(1.0f, glm::abs(glm::dot(a, b)));
    return glm::degrees(2.0f * glm::acos(d));
}

void NetworkComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    uint32 id = info.id;
    if (id >= 0x80000000u)
    {
        Log::warning("Network: entity '" + std::string(entity.getName()) + "' authored Id " + std::to_string(id)
            + " is in the reserved dynamic-spawn range, masking to " + std::to_string(id & 0x7fffffffu));
        id &= 0x7fffffffu;
    }
    if (id == 0)
        id = Globals::networkManager.deriveAutoId(entity);
    // registration runs in every role (None included): the registry is what pairs entities when a
    // session starts, and it keeps editor respawns consistent
    netId = id;
    Globals::networkManager.registerEntity(netId, &entity, this);
    lastSentPos = glm::vec3(FLT_MAX); // force the first change-detection send
}

void NetworkComponent::destroy(Entity& entity, const SpawnInfo&)
{
    Globals::networkManager.unregisterEntity(netId, this);
}

void NetworkComponent::update(Entity& entity, float deltaSeconds)
{
    if (Globals::networkManager.role() != ENetRole::Client || !hasTarget)
        return;

    timeSinceSnapshot += deltaSeconds;
    const NetSyncParams& params = Globals::networkManager.params();
    const bool newSnapshot = serverTick != lastAppliedTick;

    PhysicsComponent* physics = getComponent<PhysicsComponent>(&entity);
    if (physics && physics->bodyType == EPhysicsBodyType::Dynamic && physics->body.isValid())
    {
        // The BODY owns the pose (PhysicsComponent::update rewrites entity.pos/rot from it right after
        // this), so corrections target the body through the thread-safe command queue — this runs on a
        // job worker inside the parallel entity pass, and even a direct velocity setter would wake the
        // body, mutating box3d's shared solver sets.
        if (!(targetFlags & NetRecFlag_Physics))
            return; // the server's twin has no live dynamic body (suspended there?) — don't fight it
        PhysicsWorld& physicsWorld = Globals::physics;
        const glm::vec3 bodyPos = physics->body.getPosition();
        const glm::quat bodyRot = physics->body.getRotation();

        if (targetFlags & NetRecFlag_Asleep)
        {
            if (!newSnapshot)
                return;
            lastAppliedTick = serverTick;
            // hard-sync to the server's rest pose, then sleep our body too (asleep records only
            // refresh on keyframes, so this path is rare)
            if (glm::length(targetPos - bodyPos) > params.posDeadzone || quatAngleDeg(bodyRot, targetRot) > params.rotDeadzoneDeg)
            {
                physicsWorld.teleportBody(physics->body, targetPos, targetRot);
                physicsWorld.queueBodyCommand(physics->body, PhysicsWorld::EBodyCommand::SetLinearVelocity, glm::vec3(0.0f));
                physicsWorld.queueBodyCommand(physics->body, PhysicsWorld::EBodyCommand::SetAngularVelocity, glm::vec3(0.0f));
                physics->prevPos = physics->currPos = targetPos;
                physics->prevRot = physics->currRot = targetRot;
            }
            physicsWorld.queueBodyCommand(physics->body, PhysicsWorld::EBodyCommand::SetAwake, glm::vec3(0.0f));
            return;
        }

        glm::vec3 target = targetPos;
        if (params.extrapolate)
            target += targetLinVel * glm::min(timeSinceSnapshot, 0.25f); // dead-reckon between snapshots, capped

        const float posErr = glm::length(target - bodyPos);
        const float rotErrDeg = quatAngleDeg(bodyRot, targetRot);
        const bool snap = posErr > params.posSnapThreshold || rotErrDeg > params.rotSnapThresholdDeg;
        if (snap || posErr > params.posDeadzone || rotErrDeg > params.rotDeadzoneDeg)
        {
            glm::vec3 newPos;
            glm::quat newRot;
            if (snap)
            {
                newPos = target;
                newRot = targetRot;
            }
            else
            {
                const float blend = 1.0f - glm::exp(-params.blendRate * deltaSeconds);
                newPos = glm::mix(bodyPos, target, blend);
                newRot = glm::slerp(bodyRot, targetRot, blend);
            }
            physicsWorld.teleportBody(physics->body, newPos, newRot);
            // The teleport applies at the NEXT physics.update and prev/curr only refresh on step
            // boundaries — stomping them makes PhysicsComponent::update (right after this, same
            // entity) show the corrected pose this frame instead of lerping from stale state.
            physics->prevPos = physics->currPos = newPos;
            physics->prevRot = physics->currRot = newRot;
        }
        if (newSnapshot)
        {
            lastAppliedTick = serverTick;
            if (params.syncVelocities)
            {
                // once per snapshot, not per frame: between snapshots the local sim integrates freely
                physicsWorld.queueBodyCommand(physics->body, PhysicsWorld::EBodyCommand::SetLinearVelocity, targetLinVel);
                physicsWorld.queueBodyCommand(physics->body, PhysicsWorld::EBodyCommand::SetAngularVelocity, targetAngVel);
            }
        }
        return;
    }

    // non-physics (or kinematic/static body: the entity transform syncs, the collider stays put —
    // teleportBody is the engine-wide rule for moving colliders); targets are entity-LOCAL here
    const float posErr = glm::length(targetPos - entity.pos);
    const float rotErrDeg = quatAngleDeg(entity.rot, targetRot);

    if (posErr > params.posSnapThreshold || rotErrDeg > params.rotSnapThresholdDeg)
    {
        entity.pos = targetPos;
        entity.rot = targetRot;
        return;
    }
    // exponential blend toward the target; inside the deadzone the local sim free-runs
    const float blend = 1.0f - glm::exp(-params.blendRate * deltaSeconds);
    if (posErr > params.posDeadzone)
        entity.pos = glm::mix(entity.pos, targetPos, blend);
    if (rotErrDeg > params.rotDeadzoneDeg)
        entity.rot = glm::slerp(entity.rot, targetRot, blend);
}

const NetworkComponent::SpawnInfo* getNetworkSpawnInfo(const Entity* entity)
{
    if (!entity->spawnTemplate || !hasComponent<NetworkComponent>(entity))
        return nullptr;

    size_t idx = 0;
    for (uint16 i = 0; i < uint16(EComponentID_Network); ++i)
        if (entity->typeBits & (1 << i))
            ++idx;
    if (idx >= entity->spawnTemplate->spawnInfos.size())
        return nullptr;
    return static_cast<const NetworkComponent::SpawnInfo*>(entity->spawnTemplate->spawnInfos[idx].get());
}

void writeNetworkSpawnInfo(const NetworkComponent::SpawnInfo& info, AssetNode& out)
{
    // always emitted: Prefab.cpp drops component nodes with no children entirely
    out.addChild("Id").values.emplace_back(std::to_string(info.id));
}
