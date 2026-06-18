module Entity;

import Core;
import Core.Transform;
import Entity.Component;
import Entity.Allocator;

EntityArchetype makeEntityArchetype(uint16 typeBits)
{
    return EntityArchetype{ uint16(getEntityAllocSize(typeBits)), typeBits };
}

EntityPtr createEntity(const EntitySpawnTemplate& tmpl, const Transform& transform)
{
    void* buffer = Globals::entityAllocator.allocate(tmpl.archetype.allocSize);

    Entity* entity = ::new (buffer) Entity();
    entity->pos = transform.pos;
    entity->scale = transform.scale;
    entity->rot = transform.quat;

    entity->name = tmpl.displayName;
    entity->spawnTemplate = &tmpl;
    entity->typeBits = tmpl.archetype.typeBits;

    int idx = 0;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            createComponent(entity, EComponentID(i), tmpl.spawnInfos[idx++].get(), transform);

    return EntityPtr(entity);
}

void destroyEntity(Entity* entity)
{
    const uint32 size = getEntityAllocSize(entity->typeBits);

    int idx = 0;
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            destroyComponent(entity, EComponentID(i), entity->spawnTemplate->spawnInfos[idx++].get());

    entity->~Entity();
    Globals::entityAllocator.deallocate(entity, size);
}
