export module Core.Animation;

import Core;
import Core.glm;
import Core.Skeleton;

export struct PositionKey { float time; glm::vec3 value; };
export struct RotationKey { float time; glm::quat value; };
export struct ScaleKey    { float time; glm::vec3 value; };

// One animated bone track. Keys are sorted by time (in seconds). boneIndex is resolved against the
// Skeleton the clip was built for (-1 = the channel targets a node not present in the skeleton).
export struct AnimationChannel
{
    int32 boneIndex = -1;
    std::vector<PositionKey> positionKeys;
    std::vector<RotationKey> rotationKeys;
    std::vector<ScaleKey>    scaleKeys;
};

export struct AnimationClip
{
    std::string name;
    float duration = 0.0f; // seconds
    std::vector<AnimationChannel> channels;
};

// Samples a single AnimationClip against a Skeleton each tick into a bone palette (one mat4 per bone,
// = globalBoneTransform * inverseBind). The palette deforms mesh-space vertices into the posed mesh
// space; GPU skinning consumes it. CPU-side, single-clip looped playback (no blending).
export class AnimationPlayer
{
public:
    void initialize(const Skeleton* pSkeleton, const AnimationClip* pClip = nullptr);
    void setClip(const AnimationClip* pClip);

    void tick(float deltaSeconds);
    void setTime(float seconds);

    void setSpeed(float speed) { m_speed = speed; }
    void setPaused(bool paused) { m_paused = paused; }
    float getTime() const { return m_time; }

    std::span<const glm::mat4> getPalette() const { return m_palette; }
    uint32 getNumBones() const { return (uint32)m_palette.size(); }

    // ---- Programmatic bone posing -------------------------------------------------------------------
    // Bones are addressed by name (resolved against the Skeleton) or index. Modifiers are sticky: they
    // apply every tick until cleared, and take effect on the next tick() (which the app calls per frame,
    // even while paused). All transforms are the bone's LOCAL (parent-relative) space.
    const Skeleton* getSkeleton() const { return m_pSkeleton; }
    int32 findBone(const std::string& name) const;

    // Override: replaces the bone's local transform entirely (ignores the clip/bind for that bone).
    void setBoneTransform(const std::string& name, const glm::mat4& localTransform);
    void setBoneTransform(uint32 boneIndex, const glm::mat4& localTransform);
    void setBoneTransform(const std::string& name, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale = glm::vec3(1.0f));

    // Additive: layered on top of the clip/bind pose for that bone (post-multiplied, i.e. applied in the
    // bone's local frame). Use the quat overload to simply rotate a bone relative to its animated pose.
    void setBoneOffset(const std::string& name, const glm::mat4& localOffset);
    void setBoneOffset(uint32 boneIndex, const glm::mat4& localOffset);
    void setBoneOffset(const std::string& name, const glm::quat& deltaRotation);

    void clearBoneModifier(const std::string& name);
    void clearBoneModifier(uint32 boneIndex);
    void clearBoneModifiers();

private:
    void evaluate();

    enum class BoneModifierMode : uint8 { None, Override, Additive };
    struct BoneModifier { glm::mat4 transform = glm::mat4(1.0f); BoneModifierMode mode = BoneModifierMode::None; };

    const Skeleton* m_pSkeleton = nullptr;
    const AnimationClip* m_pClip = nullptr;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_paused = false;
    std::vector<glm::mat4> m_globalTransforms; // scratch: per-bone composed global transform
    std::vector<glm::mat4> m_palette;
    std::vector<BoneModifier> m_boneModifiers; // per-bone, sized to the skeleton; None unless posed
    bool m_anyBoneModifier = false;            // fast skip in evaluate() when nothing is posed
};
