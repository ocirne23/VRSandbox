module;

#include <immintrin.h>

module Spatial;

import Core;
import Core.glm;
import Core.Frustum;

// Shared hierarchical traversal: descend the per-level grids top-down through the occupancy
// masks, classifying each cell's loose bounds (inflated by half a cell Ã¢â‚¬â€ entities extend at most
// that far beyond their cell) as outside / intersecting / fully inside. Fully-inside subtrees
// append their entries without further per-entity tests. All bounds math is float relative to a
// double reference position, so tests stay exact at planet-scale coordinates.

namespace
{
    enum class EIntersect : uint8 { Outside, Intersect, Inside };

    struct SphereTester // query volume centered on refPos
    {
        float radius;

        EIntersect classify(const glm::vec3& cellCenter, const glm::vec3& halfExtent) const
        {
            const glm::vec3 d = glm::abs(cellCenter);
            const glm::vec3 closest = glm::max(d - halfExtent, glm::vec3(0.0f));
            if (glm::dot(closest, closest) > radius * radius)
                return EIntersect::Outside;
            const glm::vec3 farthest = d + halfExtent;
            if (glm::dot(farthest, farthest) <= radius * radius)
                return EIntersect::Inside;
            return EIntersect::Intersect;
        }

        bool testEntity(const glm::vec3& pos, float entityRadius) const
        {
            const float r = radius + entityRadius;
            return glm::dot(pos, pos) <= r * r;
        }

        bool acceptCell(const glm::vec3&, const glm::vec3&) const { return true; }
    };

    struct AABBTester // box centered on refPos
    {
        glm::vec3 half;

        EIntersect classify(const glm::vec3& cellCenter, const glm::vec3& halfExtent) const
        {
            const glm::vec3 d = glm::abs(cellCenter);
            if (d.x - halfExtent.x > half.x || d.y - halfExtent.y > half.y || d.z - halfExtent.z > half.z)
                return EIntersect::Outside;
            if (d.x + halfExtent.x <= half.x && d.y + halfExtent.y <= half.y && d.z + halfExtent.z <= half.z)
                return EIntersect::Inside;
            return EIntersect::Intersect;
        }

        bool testEntity(const glm::vec3& pos, float entityRadius) const
        {
            const glm::vec3 closest = glm::max(glm::abs(pos) - half, glm::vec3(0.0f));
            return glm::dot(closest, closest) <= entityRadius * entityRadius;
        }

        bool acceptCell(const glm::vec3&, const glm::vec3&) const { return true; }
    };

    struct FrustumTester // frustum + distance band, both camera-relative (refPos = camera)
    {
        const Frustum& frustum;
        IOcclusionTester* occlusion;
        float maxDist;

        EIntersect classify(const glm::vec3& cellCenter, const glm::vec3& halfExtent) const
        {
            const glm::vec3 d = glm::abs(cellCenter);
            const glm::vec3 closest = glm::max(d - halfExtent, glm::vec3(0.0f));
            if (glm::dot(closest, closest) > maxDist * maxDist)
                return EIntersect::Outside;
            const EFrustumTest frustumResult = frustum.testAABB(cellCenter, halfExtent);
            if (frustumResult == EFrustumTest::Outside)
                return EIntersect::Outside;
            if (occlusion && !occlusion->isVisible(cellCenter, halfExtent))
                return EIntersect::Outside;
            if (frustumResult == EFrustumTest::Inside)
            {
                const glm::vec3 farthest = d + halfExtent;
                if (glm::dot(farthest, farthest) <= maxDist * maxDist)
                    return EIntersect::Inside;
            }
            return EIntersect::Intersect;
        }

        bool testEntity(const glm::vec3& pos, float entityRadius) const
        {
            const float r = maxDist + entityRadius;
            return glm::dot(pos, pos) <= r * r && frustum.sphereInFrustum(pos, entityRadius);
        }

        bool acceptCell(const glm::vec3& cellCenter, const glm::vec3& halfExtent) const
        {
            return !occlusion || occlusion->isVisible(cellCenter, halfExtent);
        }

        // 8 spheres at once against the distance band + 6 planes; lanes with negative radius
        // (tombstones) or non-matching layers drop out. Returns an 8-bit hit mask.
        uint32 test8(const glm::vec3& cellMin, const float* px, const float* py, const float* pz,
                     const float* pr, const uint32* layers, uint32 layerMask) const
        {
            const __m256 x = _mm256_add_ps(_mm256_loadu_ps(px), _mm256_set1_ps(cellMin.x));
            const __m256 y = _mm256_add_ps(_mm256_loadu_ps(py), _mm256_set1_ps(cellMin.y));
            const __m256 z = _mm256_add_ps(_mm256_loadu_ps(pz), _mm256_set1_ps(cellMin.z));
            const __m256 r = _mm256_loadu_ps(pr);
            __m256 ok = _mm256_cmp_ps(r, _mm256_setzero_ps(), _CMP_GE_OQ);
            const __m256 d2 = _mm256_fmadd_ps(x, x, _mm256_fmadd_ps(y, y, _mm256_mul_ps(z, z)));
            const __m256 rr = _mm256_add_ps(_mm256_set1_ps(maxDist), r);
            ok = _mm256_and_ps(ok, _mm256_cmp_ps(d2, _mm256_mul_ps(rr, rr), _CMP_LE_OQ));
            for (uint32 p = 0; p < 6; ++p)
            {
                const glm::vec4& plane = frustum.planes[p];
                const __m256 d = _mm256_fmadd_ps(_mm256_set1_ps(plane.x), x,
                                 _mm256_fmadd_ps(_mm256_set1_ps(plane.y), y,
                                 _mm256_fmadd_ps(_mm256_set1_ps(plane.z), z, _mm256_set1_ps(plane.w))));
                ok = _mm256_and_ps(ok, _mm256_cmp_ps(d, _mm256_sub_ps(_mm256_setzero_ps(), r), _CMP_GE_OQ));
            }
            const __m256i lay = _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(layers)),
                                                 _mm256_set1_epi32(int(layerMask)));
            ok = _mm256_andnot_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(lay, _mm256_setzero_si256())), ok);
            return uint32(_mm256_movemask_ps(ok));
        }
    };

    struct SafeCullTester // frustum Ã¢Ë†Âª camera ball: nearby off-screen casters keep shadows/GI alive
    {
        FrustumTester frustumTester;
        SphereTester sphereTester;

        EIntersect classify(const glm::vec3& cellCenter, const glm::vec3& halfExtent) const
        {
            const EIntersect ball = sphereTester.classify(cellCenter, halfExtent);
            if (ball == EIntersect::Inside)
                return EIntersect::Inside;
            const EIntersect frustumResult = frustumTester.classify(cellCenter, halfExtent);
            if (frustumResult == EIntersect::Inside || (ball == EIntersect::Outside && frustumResult == EIntersect::Outside))
                return frustumResult;
            return EIntersect::Intersect;
        }

        bool testEntity(const glm::vec3& pos, float entityRadius) const
        {
            return sphereTester.testEntity(pos, entityRadius) || frustumTester.testEntity(pos, entityRadius);
        }

        bool acceptCell(const glm::vec3& cellCenter, const glm::vec3& halfExtent) const
        {
            return sphereTester.classify(cellCenter, halfExtent) != EIntersect::Outside
                || frustumTester.acceptCell(cellCenter, halfExtent);
        }

        uint32 test8(const glm::vec3& cellMin, const float* px, const float* py, const float* pz,
                     const float* pr, const uint32* layers, uint32 layerMask) const
        {
            const __m256 x = _mm256_add_ps(_mm256_loadu_ps(px), _mm256_set1_ps(cellMin.x));
            const __m256 y = _mm256_add_ps(_mm256_loadu_ps(py), _mm256_set1_ps(cellMin.y));
            const __m256 z = _mm256_add_ps(_mm256_loadu_ps(pz), _mm256_set1_ps(cellMin.z));
            const __m256 r = _mm256_loadu_ps(pr);
            __m256 ok = _mm256_cmp_ps(r, _mm256_setzero_ps(), _CMP_GE_OQ);
            const __m256 d2 = _mm256_fmadd_ps(x, x, _mm256_fmadd_ps(y, y, _mm256_mul_ps(z, z)));
            const __m256 rr = _mm256_add_ps(_mm256_set1_ps(sphereTester.radius), r);
            ok = _mm256_and_ps(ok, _mm256_cmp_ps(d2, _mm256_mul_ps(rr, rr), _CMP_LE_OQ));
            const __m256i lay = _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(layers)),
                                                 _mm256_set1_epi32(int(layerMask)));
            ok = _mm256_andnot_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(lay, _mm256_setzero_si256())), ok);
            return uint32(_mm256_movemask_ps(ok)) // inside the camera ball, or in the frustum
                 | frustumTester.test8(cellMin, px, py, pz, pr, layers, layerMask);
        }
    };

    struct RayTester // segment [0, maxDist] along dir from refPos; cells are never fully inside
    {
        glm::vec3 dir; // normalized
        float maxDist;

        EIntersect classify(const glm::vec3& cellCenter, const glm::vec3& halfExtent) const
        {
            float tMin = 0.0f;
            float tMax = maxDist;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (glm::abs(dir[axis]) < 1e-8f)
                {
                    if (glm::abs(cellCenter[axis]) > halfExtent[axis])
                        return EIntersect::Outside;
                }
                else
                {
                    const float inv = 1.0f / dir[axis];
                    float t0 = (cellCenter[axis] - halfExtent[axis]) * inv;
                    float t1 = (cellCenter[axis] + halfExtent[axis]) * inv;
                    if (t0 > t1)
                    {
                        const float tmp = t0;
                        t0 = t1;
                        t1 = tmp;
                    }
                    tMin = glm::max(tMin, t0);
                    tMax = glm::min(tMax, t1);
                    if (tMin > tMax)
                        return EIntersect::Outside;
                }
            }
            return EIntersect::Intersect;
        }

        bool testEntity(const glm::vec3& pos, float entityRadius) const
        {
            const float t = glm::clamp(glm::dot(pos, dir), 0.0f, maxDist);
            const glm::vec3 closest = pos - dir * t;
            return glm::dot(closest, closest) <= entityRadius * entityRadius;
        }

        bool acceptCell(const glm::vec3&, const glm::vec3&) const { return true; }
    };

    template <typename Tester>
    concept HasTest8 = requires(const Tester& t, const glm::vec3& v, const float* f, const uint32* u, uint32 m)
    {
        t.test8(v, f, f, f, f, u, m);
    };
}

template <typename Tester, typename EmitFunc>
void SpatialIndex::traverseCell(const Tester& tester, const glm::dvec3& refPos, uint32 layerMask,
                                uint64 key, uint32 level, const CellRecord& rec, bool fullyInside, const EmitFunc& emit) const
{
    const float halfCell = float(Morton::cellSize(level) * 0.5);
    const glm::vec3 cellMin = glm::vec3(Morton::cellMinWorld(key, level) - refPos);
    float loose = halfCell;
    if (level == m_numLevels - 1 && m_topLevelMaxRadius > halfCell)
        loose = m_topLevelMaxRadius; // clamped-oversize entries can stick out further
    if (!fullyInside)
    {
        ++m_stats.cellsTested;
        const EIntersect result = tester.classify(cellMin + glm::vec3(halfCell), glm::vec3(halfCell + loose));
        if (result == EIntersect::Outside)
            return;
        fullyInside = result == EIntersect::Inside;
        m_stats.cellsFullyInside += fullyInside ? 1 : 0;
    }
    else if (!tester.acceptCell(cellMin + glm::vec3(halfCell), glm::vec3(halfCell + loose)))
        return; // occlusion still prunes inside fully-contained subtrees
    for (uint32 idx = rec.dynHead; idx != UINT32_MAX; idx = m_pool.next[idx])
    {
        if (!(m_pool.layerMask[idx] & layerMask))
            continue;
        const float entityRadius = m_pool.radius[idx];
        if (entityRadius < 0.0f)
            continue; // neutralized, pending free
        const glm::vec3 pos = cellMin + glm::vec3(m_pool.posX[idx], m_pool.posY[idx], m_pool.posZ[idx]);
        if (!fullyInside)
        {
            ++m_stats.entityTests;
            if (!tester.testEntity(pos, entityRadius))
                continue;
        }
        emit(idx, pos);
    }
    if (rec.staticCount)
    {
        const StaticStore& store = m_static[level];
        const uint32 first = rec.staticStart;
        const uint32 last = first + rec.staticCount;
        const auto testStaticScalar = [&](uint32 i)
        {
            if (store.radius[i] < 0.0f || !(store.layer[i] & layerMask))
                return;
            ++m_stats.entityTests;
            const glm::vec3 pos = cellMin + glm::vec3(store.posX[i], store.posY[i], store.posZ[i]);
            if (tester.testEntity(pos, store.radius[i]))
                emit(store.poolIdx[i], pos);
        };
        if (fullyInside)
        {
            for (uint32 i = first; i < last; ++i)
            {
                if (store.radius[i] < 0.0f || !(store.layer[i] & layerMask))
                    continue;
                emit(store.poolIdx[i], cellMin + glm::vec3(store.posX[i], store.posY[i], store.posZ[i]));
            }
        }
        else if constexpr (HasTest8<Tester>)
        {
            uint32 i = first;
            for (; i + 8 <= last; i += 8)
            {
                m_stats.entityTests += 8;
                uint32 hits = tester.test8(cellMin, &store.posX[i], &store.posY[i], &store.posZ[i],
                                           &store.radius[i], &store.layer[i], layerMask);
                while (hits)
                {
                    const uint32 lane = uint32(_tzcnt_u32(hits));
                    hits &= hits - 1;
                    const uint32 s = i + lane;
                    emit(store.poolIdx[s], cellMin + glm::vec3(store.posX[s], store.posY[s], store.posZ[s]));
                }
            }
            for (; i < last; ++i)
                testStaticScalar(i);
        }
        else
        {
            for (uint32 i = first; i < last; ++i)
                testStaticScalar(i);
        }
    }
    if (level == 0)
        return;
    uint64 childMask = rec.childMask;
    const CellMap& childLevel = m_levels[level - 1];
    while (childMask)
    {
        const uint32 bit = uint32(_tzcnt_u64(childMask));
        childMask &= childMask - 1;
        const uint64 childKey = (key << 6) | bit;
        if (const CellRecord* child = childLevel.find(childKey))
            traverseCell(tester, refPos, layerMask, childKey, level - 1, *child, fullyInside, emit);
    }
}

template <typename Tester, typename EmitFunc>
void SpatialIndex::traverse(const Tester& tester, const glm::dvec3& refPos, uint32 layerMask, const EmitFunc& emit) const
{
    const uint32 top = m_numLevels - 1;
    m_levels[top].forEachCell([&](uint64 key, const CellRecord& rec)
    {
        traverseCell(tester, refPos, layerMask, key, top, rec, false, emit);
    });
}

uint32 SpatialIndex::querySphere(const glm::dvec3& center, float radius, uint32 layerMask, std::vector<uint64>& outUserData) const
{
    outUserData.clear();
    traverse(SphereTester{ radius }, center, layerMask,
        [&](uint32 idx, const glm::vec3&) { outUserData.push_back(m_pool.userData[idx]); });
    return uint32(outUserData.size());
}

uint32 SpatialIndex::queryAABB(const glm::dvec3& boxMin, const glm::dvec3& boxMax, uint32 layerMask, std::vector<uint64>& outUserData) const
{
    outUserData.clear();
    const glm::dvec3 center = (boxMin + boxMax) * 0.5;
    traverse(AABBTester{ glm::vec3((boxMax - boxMin) * 0.5) }, center, layerMask,
        [&](uint32 idx, const glm::vec3&) { outUserData.push_back(m_pool.userData[idx]); });
    return uint32(outUserData.size());
}

uint32 SpatialIndex::queryFrustum(const Frustum& frustumRelCamera, const glm::dvec3& cameraPos, float maxDist, uint32 layerMask,
                                  std::vector<uint64>& outUserData, IOcclusionTester* occlusion) const
{
    outUserData.clear();
    traverse(FrustumTester{ frustumRelCamera, occlusion, maxDist }, cameraPos, layerMask,
        [&](uint32 idx, const glm::vec3&) { outUserData.push_back(m_pool.userData[idx]); });
    return uint32(outUserData.size());
}

uint32 SpatialIndex::queryRay(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist, uint32 layerMask,
                              std::vector<uint64>& outUserData) const
{
    outUserData.clear();
    const glm::dvec3 normalized = glm::normalize(dir);
    traverse(RayTester{ glm::vec3(normalized), float(maxDist) }, origin, layerMask,
        [&](uint32 idx, const glm::vec3&) { outUserData.push_back(m_pool.userData[idx]); });
    return uint32(outUserData.size());
}

uint64 SpatialIndex::queryNearest(const glm::dvec3& pos, float maxRadius, uint32 layerMask, uint64 excludeUserData) const
{
    uint64 best = 0;
    float bestDist = FLT_MAX;
    bool found = false;
    const auto collectNearest = [&](uint32 idx, const glm::vec3& rel)
    {
        if (m_pool.userData[idx] == excludeUserData)
            return;
        const float dist = glm::length(rel);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = m_pool.userData[idx];
            found = true;
        }
    };
    float searchRadius = glm::min(maxRadius, float(Morton::cellSize(1)));
    while (true)
    {
        traverse(SphereTester{ searchRadius }, pos, layerMask, collectNearest);
        if (found || searchRadius >= maxRadius)
            break;
        searchRadius = glm::min(searchRadius * 4.0f, maxRadius);
    }
    if (found && bestDist > searchRadius)
    {
        // best came from an oversized bound sticking into the search ball; anything with a nearer
        // center is guaranteed found by searching out to that center distance
        traverse(SphereTester{ glm::min(bestDist, maxRadius) }, pos, layerMask, collectNearest);
    }
    return found ? best : 0;
}

void SpatialIndex::markVisibleSet(const Frustum& frustumRelCamera, const glm::dvec3& cameraPos, float maxDist, uint32 layerMask,
                                  IOcclusionTester* occlusion, float safeRadius)
{
    const auto start = Clock::now();
    if (++m_visibleQueryId == 0) // 0 is reserved for never-stamped entries
        ++m_visibleQueryId;
    uint32 count = 0;
    const auto stamp = [&](uint32 idx, const glm::vec3&)
    {
        m_pool.lastVisibleFrame[idx] = m_visibleQueryId;
        ++count;
    };
    if (safeRadius > 0.0f)
        traverse(SafeCullTester{ { frustumRelCamera, occlusion, maxDist }, { safeRadius } }, cameraPos, layerMask, stamp);
    else
        traverse(FrustumTester{ frustumRelCamera, occlusion, maxDist }, cameraPos, layerMask, stamp);
    m_stats.visibleCount = int(count);
    m_stats.markVisibleMs = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}
