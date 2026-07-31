module Entity;

import Core;
import Core.glm;
import Core.Transform;
import :Entity;
import Particle;

void ParticleComponent::spawn(Entity& entity, const SpawnInfo& info, const Transform& base)
{
    if (info.effectPath.empty())
        return;
    effect = Globals::particleSystem.createEffect(info.effectPath, base.pos, base.quat);
    if (effect.isValid() && !info.emitting)
        effect.setEmitting(false);
}

void ParticleComponent::destroy(Entity& entity, const SpawnInfo&)
{
    effect.destroy(); // live particles retire over the next frames
}

void ParticleComponent::update(Entity& entity, const Transform& world, float deltaSeconds)
{
    if (!effect.isValid())
        return;
    effect.setTransform(world.pos, world.quat);
    if (hasLastPos && deltaSeconds > 1e-5f)
        effect.setVelocity((world.pos - lastPos) / deltaSeconds);
    lastPos = world.pos;
    hasLastPos = true;
}

const ParticleComponent::SpawnInfo* getParticleSpawnInfo(const Entity* entity)
{
    if (!entity->spawnTemplate || !hasComponent<ParticleComponent>(entity))
        return nullptr;

    size_t idx = 0;
    for (uint16 i = 0; i < uint16(EComponentID_Particle); ++i)
        if (entity->typeBits & (1 << i))
            ++idx;
    if (idx >= entity->spawnTemplate->spawnInfos.size())
        return nullptr;
    return static_cast<const ParticleComponent::SpawnInfo*>(entity->spawnTemplate->spawnInfos[idx].get());
}

void writeParticleSpawnInfo(const ParticleComponent::SpawnInfo& info, AssetNode& out)
{
    if (info.effectPath.empty())
        return;
    out.set("Effect", info.effectPath);
    if (!info.emitting)
        out.set("Emitting", info.emitting);
}
