export module Physics;

import Core;
import Core.glm;
import Core.Transform;

// Thin wrapper around the box3d rigid body simulation. The box3d C API stays private to this
// library: bodies are referenced through PhysicsBody handles (b3BodyId bits stored as uint64).

export enum class EPhysicsBodyType : uint8
{
    Static,
    Kinematic,
    Dynamic,
};

export enum class EPhysicsShapeType : uint8
{
    Box,
    Sphere,
    Capsule,
};

// Named collision layers. A name maps to one of 64 category bits, allocated on first use;
// "Default" is bit 0 (the categoryBits/maskBits defaults below). Two shapes collide when each one's
// mask contains the other's category (unless a non-zero matching groupIndex overrides: negative =
// never collide, positive = always).
export namespace PhysicsLayers
{
    uint64 bit(std::string_view name);
    constexpr uint64 All = ~0ull;
}

export struct PhysicsShape
{
    EPhysicsShapeType type = EPhysicsShapeType::Box;
    glm::vec3 halfExtents = glm::vec3(0.5f); // Box
    float radius = 0.5f;                     // Sphere / Capsule
    float halfHeight = 0.5f;                 // Capsule: center to hemisphere center, along local Y
    glm::vec3 offset = glm::vec3(0.0f);      // placement within the body's local space
    float density = 1000.0f;                 // kg/m^3
    float friction = 0.6f;
    float restitution = 0.0f;
    uint64 categoryBits = 1;                 // the layer(s) this shape is on (bit 0 = "Default")
    uint64 maskBits = PhysicsLayers::All;    // the layers this shape collides with
    int groupIndex = 0;                      // box3d collision group override, 0 = none
};

export struct PhysicsBodyDesc
{
    EPhysicsBodyType type = EPhysicsBodyType::Dynamic;
    Transform transform;                          // world placement; transform.scale is baked into the shape dimensions
    glm::vec3 linearVelocity = glm::vec3(0.0f);
    void* userData = nullptr;
};

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

export class PhysicsWorld final
{
public:

    ~PhysicsWorld() { shutdown(); }

    bool initialize();
    void shutdown();

    // Fixed-step accumulator; steps at stepHz with a bounded number of catch-up steps per frame.
    void update(double deltaSec);

    PhysicsBody createBody(const PhysicsBodyDesc& desc, std::span<const PhysicsShape> shapes);

    struct RayHit
    {
        bool hit = false;
        float fraction = 0.0f;
        glm::vec3 point = glm::vec3(0.0f);
        glm::vec3 normal = glm::vec3(0.0f);
    };
    RayHit castRayClosest(const glm::vec3& origin, const glm::vec3& translation, uint64 maskBits = PhysicsLayers::All) const;

    void setGravity(const glm::vec3& gravity);

    bool isInitialized() const { return m_initialized; }

private:

    uint32 m_worldHandle = 0; // b3WorldId bits
    bool m_initialized = false;
    bool m_paused = false;
    float m_accumulator = 0.0f;
    float m_timeScale = 1.0f;
    int m_subSteps = 4;
    int m_stepHz = 60;
    glm::vec3 m_gravity = glm::vec3(0.0f, -9.81f, 0.0f);
};

export namespace Globals
{
    PhysicsWorld physics;
}
