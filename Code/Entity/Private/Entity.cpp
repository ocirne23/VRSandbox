module Entity;

import Core;
import Core.Transform;
import Entity.Component;
import Entity.Allocator;

EntityArchetype makeEntityArchetype(uint16 typeBits)
{
    return EntityArchetype{ uint16(getEntityAllocSize(typeBits)), typeBits };
}

EntityPtr createEntity(const EntityArchetype& archetype, const Transform& transform)
{
    void* buffer = Globals::entityAllocator.allocate(archetype.allocSize);

    Entity* entity = ::new (buffer) Entity();
    entity->pos = transform.pos;
    entity->scale = transform.scale;
    entity->rot = transform.quat;
    entity->typeBits = archetype.typeBits;

    constructInlineComponents(entity);
    return EntityPtr(entity); // refCount 1
}

EntityPtr createEntity(uint16 typeBits, const Transform& transform)
{
    return createEntity(makeEntityArchetype(typeBits), transform);
}

void destroyEntity(Entity* entity)
{
    const uint32 size = getEntityAllocSize(entity->typeBits);
    destructInlineComponents(entity);
    entity->~Entity();
    Globals::entityAllocator.deallocate(entity, size);
}
