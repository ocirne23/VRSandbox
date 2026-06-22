export module Core.Transform;

import Core.glm;

export struct Transform
{
    Transform() : pos(0, 0, 0), scale(1.0f), quat(1, 0, 0, 0) {}
    Transform(const glm::vec3& pos, float scale, const glm::quat& quat) : pos(pos), scale(scale), quat(quat) {}
    glm::vec3 pos;
    float scale;
    glm::quat quat;

    glm::vec3 transformPoint(const glm::vec3& p) const { return pos + quat * (p * scale); }

    // Inverse of this similarity transform: this * this.inverse() == identity.
    Transform inverse() const
    {
        const float invScale = 1.0f / scale;
        const glm::quat invQuat = glm::conjugate(quat);
        return Transform(invQuat * (-pos) * invScale, invScale, invQuat);
    }

    // Compose: result applies `child` first, then `this` (this == parent frame).
    Transform operator*(const Transform& child) const
    {
        return Transform(transformPoint(child.pos), scale * child.scale, quat * child.quat);
    }
};