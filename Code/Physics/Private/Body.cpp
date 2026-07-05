module;

#include <box3d/box3d.h>

module Physics;

import Core.glm;

import :Body;
import :Convert;

void PhysicsBody::destroy()
{
    if (m_handle == 0)
        return;
    const b3BodyId id = toBodyId(m_handle);
    if (b3Body_IsValid(id)) // the world may already be gone at shutdown
        b3DestroyBody(id);
    m_handle = 0;
}

glm::vec3 PhysicsBody::getPosition() const
{
    return toGlm(b3Body_GetPosition(toBodyId(m_handle)));
}

glm::quat PhysicsBody::getRotation() const
{
    return toGlm(b3Body_GetRotation(toBodyId(m_handle)));
}

void PhysicsBody::setTransform(const glm::vec3& pos, const glm::quat& rot)
{
    b3Body_SetTransform(toBodyId(m_handle), toB3(pos), toB3(glm::normalize(rot)));
}

glm::vec3 PhysicsBody::getLinearVelocity() const
{
    return toGlm(b3Body_GetLinearVelocity(toBodyId(m_handle)));
}

void PhysicsBody::setLinearVelocity(const glm::vec3& velocity)
{
    b3Body_SetLinearVelocity(toBodyId(m_handle), toB3(velocity));
}

void PhysicsBody::applyImpulse(const glm::vec3& impulse)
{
    b3Body_ApplyLinearImpulseToCenter(toBodyId(m_handle), toB3(impulse), true);
}

bool PhysicsBody::isAwake() const
{
    return b3Body_IsAwake(toBodyId(m_handle));
}
