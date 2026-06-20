module Core.Animation;

import Core;
import Core.glm;
import Core.Skeleton;

namespace
{
    // Finds the index i such that keys[i].time <= t < keys[i+1].time, returning the interpolation factor
    // in [0,1]. Assumes keys is non-empty and sorted by time.
    template <typename KeyT>
    uint32 findKey(const std::vector<KeyT>& keys, float t, float& outFactor)
    {
        for (uint32 i = 0; i + 1 < (uint32)keys.size(); ++i)
        {
            if (t < keys[i + 1].time)
            {
                const float span = keys[i + 1].time - keys[i].time;
                outFactor = span > 1e-8f ? (t - keys[i].time) / span : 0.0f;
                return i;
            }
        }
        outFactor = 0.0f;
        return (uint32)keys.size() - 1;
    }

    glm::vec3 sampleVec(const std::vector<PositionKey>& keys, float t)
    {
        if (keys.empty()) return glm::vec3(0.0f);
        if (keys.size() == 1) return keys[0].value;
        float f;
        const uint32 i = findKey(keys, t, f);
        if (i + 1 >= (uint32)keys.size()) return keys.back().value;
        return glm::mix(keys[i].value, keys[i + 1].value, f);
    }

    glm::vec3 sampleScale(const std::vector<ScaleKey>& keys, float t)
    {
        if (keys.empty()) return glm::vec3(1.0f);
        if (keys.size() == 1) return keys[0].value;
        float f;
        const uint32 i = findKey(keys, t, f);
        if (i + 1 >= (uint32)keys.size()) return keys.back().value;
        return glm::mix(keys[i].value, keys[i + 1].value, f);
    }

    glm::quat sampleQuat(const std::vector<RotationKey>& keys, float t)
    {
        if (keys.empty()) return glm::quat(1, 0, 0, 0);
        if (keys.size() == 1) return keys[0].value;
        float f;
        const uint32 i = findKey(keys, t, f);
        if (i + 1 >= (uint32)keys.size()) return keys.back().value;
        return glm::normalize(glm::slerp(keys[i].value, keys[i + 1].value, f));
    }
}

void AnimationPlayer::initialize(const Skeleton* pSkeleton, const AnimationClip* pClip)
{
    m_pSkeleton = pSkeleton;
    const uint32 numBones = pSkeleton ? pSkeleton->numBones() : 0;
    m_globalTransforms.assign(numBones, glm::mat4(1.0f));
    m_palette.assign(numBones, glm::mat4(1.0f));
    m_boneModifiers.assign(numBones, BoneModifier{});
    m_anyBoneModifier = false;
    setClip(pClip);
}

void AnimationPlayer::setClip(const AnimationClip* pClip)
{
    m_pClip = pClip;
    m_time = 0.0f;
    evaluate();
}

void AnimationPlayer::tick(float deltaSeconds)
{
    if (!m_paused && m_pClip && m_pClip->duration > 0.0f)
    {
        m_time += deltaSeconds * m_speed;
        m_time = m_time - m_pClip->duration * std::floor(m_time / m_pClip->duration); // loop into [0,duration)
    }
    evaluate();
}

void AnimationPlayer::setTime(float seconds)
{
    m_time = seconds;
    if (m_pClip && m_pClip->duration > 0.0f)
        m_time = m_time - m_pClip->duration * std::floor(m_time / m_pClip->duration);
    evaluate();
}

void AnimationPlayer::evaluate()
{
    if (!m_pSkeleton)
        return;

    const Skeleton& skel = *m_pSkeleton;
    const uint32 numBones = skel.numBones();

    // Start every bone at its bind-pose local transform, then override animated bones with sampled TRS.
    std::vector<glm::mat4> local(skel.localBind.begin(), skel.localBind.end());
    if (m_pClip)
    {
        for (const AnimationChannel& ch : m_pClip->channels)
        {
            if (ch.boneIndex < 0 || (uint32)ch.boneIndex >= numBones)
                continue;
            const glm::vec3 pos   = sampleVec(ch.positionKeys, m_time);
            const glm::quat rot   = sampleQuat(ch.rotationKeys, m_time);
            const glm::vec3 scale = sampleScale(ch.scaleKeys, m_time);
            local[ch.boneIndex] = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scale);
        }
    }

    // Apply programmatic per-bone modifiers on top of the clip/bind pose.
    if (m_anyBoneModifier)
    {
        for (uint32 i = 0; i < numBones; ++i)
        {
            const BoneModifier& mod = m_boneModifiers[i];
            if (mod.mode == BoneModifierMode::Override)
                local[i] = mod.transform;
            else if (mod.mode == BoneModifierMode::Additive)
                local[i] = local[i] * mod.transform;
        }
    }

    // Compose global transforms (parent-before-child order guarantees the parent is already done), then
    // build the skinning palette.
    for (uint32 i = 0; i < numBones; ++i)
    {
        const int32 parent = skel.parentIndices[i];
        m_globalTransforms[i] = parent < 0 ? local[i] : m_globalTransforms[parent] * local[i];
        m_palette[i] = m_globalTransforms[i] * skel.inverseBind[i];
    }
}

int32 AnimationPlayer::findBone(const std::string& name) const
{
    return m_pSkeleton ? m_pSkeleton->findBone(name) : -1;
}

void AnimationPlayer::setBoneTransform(uint32 boneIndex, const glm::mat4& localTransform)
{
    if (boneIndex >= m_boneModifiers.size())
        return;
    m_boneModifiers[boneIndex] = BoneModifier{ localTransform, BoneModifierMode::Override };
    m_anyBoneModifier = true;
}

void AnimationPlayer::setBoneTransform(const std::string& name, const glm::mat4& localTransform)
{
    const int32 idx = findBone(name);
    if (idx >= 0)
        setBoneTransform((uint32)idx, localTransform);
}

void AnimationPlayer::setBoneTransform(const std::string& name, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale)
{
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scale);
    setBoneTransform(name, m);
}

void AnimationPlayer::setBoneOffset(uint32 boneIndex, const glm::mat4& localOffset)
{
    if (boneIndex >= m_boneModifiers.size())
        return;
    m_boneModifiers[boneIndex] = BoneModifier{ localOffset, BoneModifierMode::Additive };
    m_anyBoneModifier = true;
}

void AnimationPlayer::setBoneOffset(const std::string& name, const glm::mat4& localOffset)
{
    const int32 idx = findBone(name);
    if (idx >= 0)
        setBoneOffset((uint32)idx, localOffset);
}

void AnimationPlayer::setBoneOffset(const std::string& name, const glm::quat& deltaRotation)
{
    setBoneOffset(name, glm::mat4_cast(deltaRotation));
}

void AnimationPlayer::clearBoneModifier(uint32 boneIndex)
{
    if (boneIndex >= m_boneModifiers.size())
        return;
    m_boneModifiers[boneIndex] = BoneModifier{};
    m_anyBoneModifier = false;
    for (const BoneModifier& mod : m_boneModifiers)
        if (mod.mode != BoneModifierMode::None) { m_anyBoneModifier = true; break; }
}

void AnimationPlayer::clearBoneModifier(const std::string& name)
{
    const int32 idx = findBone(name);
    if (idx >= 0)
        clearBoneModifier((uint32)idx);
}

void AnimationPlayer::clearBoneModifiers()
{
    m_boneModifiers.assign(m_boneModifiers.size(), BoneModifier{});
    m_anyBoneModifier = false;
}
