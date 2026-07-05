export module Physics:Mesh;

import Core;

// A shared triangle-mesh collision BVH (box3d b3MeshData). Mesh shapes reference it, so it must
// outlive every body using it. Movable RAII, like PhysicsBody.
export class PhysicsMesh final
{
public:

    PhysicsMesh() = default;
    PhysicsMesh(const PhysicsMesh&) = delete;
    PhysicsMesh& operator=(const PhysicsMesh&) = delete;
    PhysicsMesh(PhysicsMesh&& other) noexcept : m_data(other.m_data) { other.m_data = 0; }
    PhysicsMesh& operator=(PhysicsMesh&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            m_data = other.m_data;
            other.m_data = 0;
        }
        return *this;
    }
    ~PhysicsMesh() { destroy(); }

    bool isValid() const { return m_data != 0; }
    void destroy();

private:

    friend class PhysicsWorld;
    uint64 m_data = 0; // b3MeshData*
};
