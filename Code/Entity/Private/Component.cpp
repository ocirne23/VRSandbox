module Entity.Component;

import Core;

void constructInlineComponents(Entity* entity)
{
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            EntityComponentDetail::inlineConstructors[i](
                reinterpret_cast<uint8*>(entity) + getComponentByteOffset(entity->typeBits, EComponentID(i)));
}

void destructInlineComponents(Entity* entity)
{
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        if (entity->typeBits & (1 << i))
            EntityComponentDetail::inlineDestructors[i](
                reinterpret_cast<uint8*>(entity) + getComponentByteOffset(entity->typeBits, EComponentID(i)));
}
