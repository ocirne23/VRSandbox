export module Physics:Joint;

import Core;

// RAII handle to a joint. Destroying an attached body destroys the joint too, so the handle
// validates before destroying.
export class PhysicsJoint final
{
public:

    PhysicsJoint() = default;
    PhysicsJoint(const PhysicsJoint&) = delete;
    PhysicsJoint& operator=(const PhysicsJoint&) = delete;
    PhysicsJoint(PhysicsJoint&& other) noexcept : m_handle(other.m_handle) { other.m_handle = 0; }
    PhysicsJoint& operator=(PhysicsJoint&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            m_handle = other.m_handle;
            other.m_handle = 0;
        }
        return *this;
    }
    ~PhysicsJoint() { destroy(); }

    bool isValid() const { return m_handle != 0; }
    void destroy();

private:

    friend class PhysicsWorld;
    explicit PhysicsJoint(uint64 handle) : m_handle(handle) {}

    uint64 m_handle = 0; // b3JointId bits, 0 = invalid
};
