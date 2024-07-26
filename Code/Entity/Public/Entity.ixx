export module Entity;

import Core;

export struct Entity;

export struct EntityID
{
    uint64_t id;

    constexpr static uint64_t PointerBits = 0b00000000'00000000'11111111'11111111'11111111'11111111'11111111'11110000;
    constexpr static uint64_t SaltBits    = 0b00000000'00000000'00000000'00000000'00000000'00000000'00000000'00001111;
    constexpr static uint64_t TypeBits    = 0b01111111'11111111'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static uint64_t isZoneBit   = 0b10000000'00000000'00000000'00000000'00000000'00000000'00000000'00000000;

    Entity* getEntity() const;
    uint8 getSalt() const
    {
        return uint8_t(id & SaltBits);
    }
    uint16 getType() const
    {
        return uint16_t((id & TypeBits) >> 48);
    }
    bool hasZone() const
    {
        return uint8_t((id & isZoneBit) != 0);
    }
};

export struct Entity
{
    glm::vec3 pos;
    float scale = 1.0f;
    glm::quat rot;
};

Entity* EntityID::getEntity() const
{
    Entity* pEntity = reinterpret_cast<Entity*>(id & PointerBits);
    return pEntity;
}

static_assert(sizeof(Entity) == 32);