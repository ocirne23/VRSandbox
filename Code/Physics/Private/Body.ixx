export module Physics:Body;

import Core;
import Core.glm;

// RAII handle to a rigid body (movable, like RenderNode). Destroying the handle destroys the body.
export class PhysicsBody final
{
public:

    PhysicsBody() = default;
    PhysicsBody(const PhysicsBody&) = delete;
    PhysicsBody& operator=(const PhysicsBody&) = delete;
    PhysicsBody(PhysicsBody&& other) noexcept : m_handle(other.m_handle) { other.m_handle = 0; }
    PhysicsBody& operator=(PhysicsBody&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            m_handle = other.m_handle;
            other.m_handle = 0;
        }
        return *this;
    }
    ~PhysicsBody() { destroy(); }

    bool isValid() const { return m_handle != 0; }
    void destroy();

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    void setTransform(const glm::vec3& pos, const glm::quat& rot);

    glm::vec3 getLinearVelocity() const;
    void setLinearVelocity(const glm::vec3& velocity);
    void applyImpulse(const glm::vec3& impulse); // at the center of mass, wakes the body

    bool isAwake() const;

private:

    friend class PhysicsWorld;
    explicit PhysicsBody(uint64 handle) : m_handle(handle) {}

    uint64 m_handle = 0; // b3BodyId bits, 0 = invalid
};
