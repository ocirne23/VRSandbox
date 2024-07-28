export module Core.AABB;

import Core;

export struct AABB final
{
    glm::vec3 min;
    glm::vec3 max;

    float getRadius() const
    {
        return glm::length(max - min) * 0.5f;
    }
};