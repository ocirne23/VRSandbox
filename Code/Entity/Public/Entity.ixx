export module Entity;
extern "C++" {

import Core;
import Core.glm;

export struct Entity;

export enum EComponentID : uint16;

export struct Entity
{
    glm::vec3 pos;
    float scale = 1.0f;
    glm::quat rot;

    uint16 refCount = 0;
    uint16 typeBits = 0;
    uint8 ecsComponentCount = 0;
    uint8 zoneIdx;

    ~Entity()
    {
        assert(refCount == 0);
    }
    template <typename T>
    T* getComponent();
    constexpr uint32 getComponentOffset(uint16 typeBits, EComponentID id);
};

export struct EntityPtr
{
    uint64 id;

    constexpr static uint64 PointerBits = 0b00000000'00000000'11111111'11111111'11111111'11111111'11111111'11110000;
    constexpr static uint64 SaltBits    = 0b00000000'00000000'00000000'00000000'00000000'00000000'00000000'00001111;
    constexpr static uint64 TypeBits    = 0b11111111'11111111'00000000'00000000'00000000'00000000'00000000'00000000;

    Entity* getEntity()
    {
        Entity* pEntity = reinterpret_cast<Entity*>(id & PointerBits);
        return pEntity;
    }

    const Entity* getEntity() const
    {
        Entity* pEntity = reinterpret_cast<Entity*>(id & PointerBits);
        return pEntity;
    }

    uint8 getSalt() const
    {
        return uint8(id & SaltBits);
    }

    uint16 getType() const
    {
        return uint16((id & TypeBits) >> 48);
    }

    EntityPtr(Entity* pEntity)
    {
        uint16 oldRefCount = std::atomic_ref<uint16>(pEntity->refCount).fetch_add(1);
        (void)oldRefCount;
        assert(oldRefCount != 0);
    }

    ~EntityPtr()
    {
        Entity* pEntity = getEntity();
        uint16 oldRefCount = std::atomic_ref<uint16>(pEntity->refCount).fetch_sub(1);
        assert(oldRefCount != 0);
        if (oldRefCount == 1)
        {
            delete pEntity;
        }
    }
};
} // extern "C++"