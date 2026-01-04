export module Entity.Zone;
extern "C++" {

import Core;
import Core.glm;
import Entity;

export struct EntityHash
{
    EntityHash() : hash(0) {}
    constexpr EntityHash(uint64 hash) : hash(hash) {}
    uint64 hash;

    constexpr static int numPosBits = 57;
    constexpr static uint64 xPosMort   = 0b00000000'00001001'00100100'10010010'01001001'00100100'10010010'01001001;
    constexpr static uint64 yPosMort   = 0b00000000'00010010'01001001'00100100'10010010'01001001'00100100'10010010;
    constexpr static uint64 zPosMort   = 0b00000000'00100100'10010010'01001001'00100100'10010010'01001001'00100100;
    constexpr static uint64 xSign      = 0b00000000'01000000'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static uint64 ySign      = 0b00000000'10000000'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static uint64 zSign      = 0b00000001'00000000'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static uint64 radiusBits = 0b11111110'00000000'00000000'00000000'00000000'00000000'00000000'00000000;

    constexpr static float posRange = 524287.0f; // +/- 524287.0f
    constexpr static float maxRadius = 128.0f * 128.0f; // 16384.0f
    constexpr static float maxError = 1.76f; // dist(vec3(0.4999), vec3(-0.5)) with some epsilon for large float math errors

    inline uint64 getRadiusMask(float radius)
    {
        return uint64(glm::ceil(glm::sqrt(radius))) - 1;
    }

    inline float getRadiusFromMask(uint64 mask)
    {
        return (float) ((mask + 1) * (mask + 1));
    }

    __declspec(noinline) void encodeMorton(const glm::vec4& positionRadius)
    {
        const uint32 xi = *(uint32*)&positionRadius.x & 0x7fffffff; // Remove sign bit before casting
        const uint32 yi = *(uint32*)&positionRadius.y & 0x7fffffff;
        const uint32 zi = *(uint32*)&positionRadius.z & 0x7fffffff;
        const uint32 x = (uint32)(*(float*)&xi); // cast float back without sign bit
        const uint32 y = (uint32)(*(float*)&yi);
        const uint32 z = (uint32)(*(float*)&zi);
        hash = _pdep_u64(x >> 1, xPosMort) | _pdep_u64(y >> 1, yPosMort) | _pdep_u64(z >> 1, zPosMort);
        hash |= (*(int*)&positionRadius.x) & 0x80000000 ? xSign : 0; // insert sign bits into hash
        hash |= (*(int*)&positionRadius.y) & 0x80000000 ? ySign : 0;
        hash |= (*(int*)&positionRadius.z) & 0x80000000 ? zSign : 0;
        hash |= getRadiusMask(positionRadius.w) << (numPosBits);
    }

    __declspec(noinline) glm::vec4 decodeMorton()
    {
        const int32 xi = (int32)(_pext_u64(hash, xPosMort) << 1);
        const int32 yi = (int32)(_pext_u64(hash, yPosMort) << 1);
        const int32 zi = (int32)(_pext_u64(hash, zPosMort) << 1);
        const float x = (float)(hash & xSign ? -xi : xi);
        const float y = (float)(hash & ySign ? -yi : yi);
        const float z = (float)(hash & zSign ? -zi : zi);
        const float radius = getRadiusFromMask(hash >> (numPosBits));
        return glm::vec4(x, y, z, radius);
    }
};

export struct EntityHash128
{
    EntityHash128() : hash0(0), hash1(0) {}
    uint64 hash0;
    uint64 hash1;

    constexpr static int numPosBits = 60;
    constexpr static uint64_t xPosLowerBits = 0b00000010'01001001'00100100'10010010'01001001'00100100'10010010'01001001;
    constexpr static uint64_t yPosLowerBits = 0b00000100'10010010'01001001'00100100'10010010'01001001'00100100'10010010;
    constexpr static uint64_t zPosLowerBits = 0b00001001'00100100'10010010'01001001'00100100'10010010'01001001'00100100;
    constexpr static uint64_t xPosUpperBits = 0b00000000'01001001'00100100'10010010'01001001'00100100'10010010'01001001;
    constexpr static uint64_t yPosUpperBits = 0b00000000'10010010'01001001'00100100'10010010'01001001'00100100'10010010;
    constexpr static uint64_t zPosUpperBits = 0b00000001'00100100'10010010'01001001'00100100'10010010'01001001'00100100;
    constexpr static uint64_t xSign         = 0b00000010'00000000'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static uint64_t ySign         = 0b00000100'00000000'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static uint64_t zSign         = 0b00001000'00000000'00000000'00000000'00000000'00000000'00000000'00000000;
    constexpr static uint64_t radiusBits    = 0b11110000'00000000'00000000'00000000'00000000'00000000'00000000'00000000;

    inline uint64 getRadiusMask(float radius)
    {
        return uint64(glm::ceil(glm::sqrt(radius))) - 1;
    }

    inline float getRadiusFromMask(uint64 mask)
    {
        return (float)((mask + 1) * (mask + 1));
    }

    __declspec(noinline) void encodeMorton(const glm::dvec4& positionRadius)
    {
        const uint64 xi = *(uint64*)&positionRadius.x & 0x7fffffff'ffffffff; // Remove sign bit before casting
        const uint64 yi = *(uint64*)&positionRadius.y & 0x7fffffff'ffffffff;
        const uint64 zi = *(uint64*)&positionRadius.z & 0x7fffffff'ffffffff;
        const uint64 x = (uint32)(*(double*)&xi); // cast without sign bit
        const uint64 y = (uint32)(*(double*)&yi);
        const uint64 z = (uint32)(*(double*)&zi);
        hash0 = _pdep_u64(x >> 1,  xPosLowerBits) | _pdep_u64(y >> 1,  yPosLowerBits) | _pdep_u64(z >> 1,  zPosLowerBits);
        hash1 = _pdep_u64(x >> 21, xPosUpperBits) | _pdep_u64(y >> 21, yPosUpperBits) | _pdep_u64(z >> 21, zPosUpperBits);
        hash1 |= (*(uint64*)&positionRadius.x) & 0x80000000'00000000 ? xSign : 0; // insert sign bits into hash
        hash1 |= (*(uint64*)&positionRadius.y) & 0x80000000'00000000 ? ySign : 0;
        hash1 |= (*(uint64*)&positionRadius.z) & 0x80000000'00000000 ? zSign : 0;
        const uint64 radiusBitsMask = getRadiusMask((float)positionRadius.w);
        hash0 |= (radiusBitsMask) << numPosBits;
        hash1 |= (radiusBitsMask & 0xF0) << (numPosBits - 4);
    }

    __declspec(noinline) glm::dvec4 decodeMorton()
    {
        const int64 xi = (int64)(_pext_u64(hash0, xPosLowerBits) | _pext_u64(hash1, xPosUpperBits) << 20) << 1;
        const int64 yi = (int64)(_pext_u64(hash0, yPosLowerBits) | _pext_u64(hash1, yPosUpperBits) << 20) << 1;
        const int64 zi = (int64)(_pext_u64(hash0, zPosLowerBits) | _pext_u64(hash1, zPosUpperBits) << 20) << 1;
        const double x = (double)(hash1 & xSign ? -xi : xi);
        const double y = (double)(hash1 & ySign ? -yi : yi);
        const double z = (double)(hash1 & zSign ? -zi : zi);
        const uint64 radiusBits0 = ((hash0 & radiusBits) >> numPosBits);
        const uint64 radiusBits1 = ((hash1 & radiusBits) >> numPosBits);
        double radius = getRadiusFromMask(radiusBits0 & 0x0F | radiusBits1 << 4);
        return glm::dvec4(x, y, z, radius);
    }
};
} // extern "C++"