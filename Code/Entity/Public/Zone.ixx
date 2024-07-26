module;

#include <cstdint>
#include <intrin.h>

export module Entity.Zone;

import Core;
import Entity;

export struct EntityHash
{
    EntityHash() : hash(0) {}
    constexpr EntityHash(uint64_t hash) : hash(hash) {}
    uint64_t hash;

    constexpr static int numPosBits = 19;
    constexpr static uint64_t xPosBits   = 0b00000000'00000000'00000000'00000000'00000000'00000111'11111111'11111111;
    constexpr static uint64_t yPosBits   = 0b00000000'00000000'00000000'00111111'11111111'11111000'00000000'00000000;
    constexpr static uint64_t zPosBits   = 0b00000001'11111111'11111111'11000000'00000000'00000000'00000000'00000000;
    constexpr static uint64_t radiusBits = 0b11111110'00000000'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static float posRange = xPosBits; // +/- 524287.0f
    constexpr static float maxRadius = 128.0f * 128.0f; // 16384.0f
    constexpr static float maxError = 1.76f; // dist(vec3(0.4999), vec3(-0.5)) with some epsilon for large float math errors

    inline uint64 getRadiusMask(float radius)
    {
        return uint64_t(glm::ceil(glm::sqrt(radius))) - 1;
    }

    inline float getRadiusFromMask(uint64 mask)
    {
        return (mask + 1) * (mask + 1) + maxError;
    }

    void encode(const glm::vec4& positionRadius)
    {
        assert(glm::abs(positionRadius.x) <= posRange
            && glm::abs(positionRadius.y) <= posRange
            && glm::abs(positionRadius.z) <= posRange
            && positionRadius.w <= maxRadius
            && positionRadius.w >= 0.0f);

        uint64_t mask = (uint64_t(positionRadius.x + posRange + 0.5f) >> 1);
        mask |= (uint64_t(positionRadius.y + posRange + 0.5f) >> 1) << numPosBits;
        mask |= (uint64_t(positionRadius.z + posRange + 0.5f) >> 1) << (numPosBits * 2);
        mask |= uint64_t(getRadiusMask(positionRadius.w)) << (numPosBits * 3);
        hash = mask;
    }

    glm::vec4 decode()
    {
        glm::vec4 pos;
        pos.x = ((float)((hash & xPosBits) << 1) - posRange) + 0.5f;
        pos.y = ((float)((hash & yPosBits) >> (numPosBits - 1)) - posRange) + 0.5f;
        pos.z = ((float)((hash & zPosBits) >> (numPosBits * 2 - 1)) - posRange) + 0.5f;
        pos.w = getRadiusFromMask(hash >> (numPosBits * 3));
        return pos;
    }
};

constexpr static int NUM_CONTAINERS = 255;

export class Zone final
{
public:
    EntityHash m_zoneHash = 0;
    uint16_t m_numChildren = 0;
    uint16_t m_depth = 0;
    int parentIndex = 0;

    EntityHash m_childHashes[NUM_CONTAINERS] = {};
    EntityID m_childIds[NUM_CONTAINERS] = {};

    EntityHash insertEntity(Entity* pEntity)
    {

        for (int i = 0; i < NUM_CONTAINERS; ++i)
        {
            EntityHash hash = m_childHashes[i];
        }
    }
};

export class ZoneSystem
{
    ZoneSystem()
    {
        m_zones.reserve(1);
        m_zones.shrink_to_fit();
        m_zones.push_back(Zone());
    }

    void insertEntity(Entity* pEntity)
    {
        m_zones[0].insertEntity(pEntity);
    }

    std::vector<EntityHash> getEntitiesInArea(const glm::dvec3& pos, float radius)
    {

    }

    Zone* getZone(uint16_t zoneIdx)
    {
        return &m_zones[zoneIdx];
    }

    std::vector<Zone> m_zones;
};