export module Physics;

import Core;
import Core.glm;
import Core.Transform;

// Thin wrapper around the box3d rigid body simulation. The box3d C API stays private to this
// library: bodies/joints/meshes are referenced through RAII handles storing the box3d id bits.

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
    Hull, // convex hull reduced from a point cloud (hullPoints)
    Mesh, // triangle-mesh BVH (mesh), static bodies only
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

    std::vector<glm::vec3> hullPoints;       // Hull: point cloud, reduced to a convex hull at body creation
    int maxHullVertices = 32;                // Hull: vertex budget for the reduction (clamped to [4, 64])
    const PhysicsMesh* mesh = nullptr;       // Mesh: shared triangle BVH, must outlive the body

    bool isSensor = false;                   // overlap events instead of collision response
    bool contactEvents = false;              // report begin/end touch events for this shape
};

export struct PhysicsBodyDesc
{
    EPhysicsBodyType type = EPhysicsBodyType::Dynamic;
    Transform transform;                          // world placement; transform.scale is baked into the shape dimensions
    glm::vec3 linearVelocity = glm::vec3(0.0f);
    void* userData = nullptr;                     // reported back through ContactEvent (the engine stores Entity* here)
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

export class PhysicsWorld final
{
public:

    ~PhysicsWorld() { shutdown(); }

    bool initialize();
    void shutdown();

    // Fixed-step accumulator; steps at stepHz with a bounded number of catch-up steps per frame.
    // Also drains contact/sensor events (see getContactEvents) and advances the step counter.
    void update(double deltaSec);

    PhysicsBody createBody(const PhysicsBodyDesc& desc, std::span<const PhysicsShape> shapes);

    // Builds a shared triangle-mesh BVH from render-style geometry (positions + triangle indices).
    // Standalone data: does not require the world and may be built before initialize().
    PhysicsMesh createCollisionMesh(std::span<const glm::vec3> vertices, std::span<const uint32> indices);

    // A shapeless static body owned by the world, for anchoring joints to the world itself.
    PhysicsBody& staticBody() { return m_staticBody; }

    // Joints. Anchors/axes are world space; local frames are derived from the bodies' current poses,
    // so create joints after the bodies are at their intended placement.
    PhysicsJoint createDistanceJoint(const PhysicsBody& a, const PhysicsBody& b,
        const glm::vec3& anchorA, const glm::vec3& anchorB, float minLength, float maxLength);
    PhysicsJoint createRevoluteJoint(const PhysicsBody& a, const PhysicsBody& b,
        const glm::vec3& pivot, const glm::vec3& axis, float lowerDeg = -FLT_MAX, float upperDeg = FLT_MAX);
    PhysicsJoint createSphericalJoint(const PhysicsBody& a, const PhysicsBody& b,
        const glm::vec3& pivot, float coneAngleDeg = 180.0f);
    PhysicsJoint createWeldJoint(const PhysicsBody& a, const PhysicsBody& b, const glm::vec3& anchor);

    struct RayHit
    {
        bool hit = false;
        float fraction = 0.0f;
        glm::vec3 point = glm::vec3(0.0f);
        glm::vec3 normal = glm::vec3(0.0f);
    };
    RayHit castRayClosest(const glm::vec3& origin, const glm::vec3& translation, uint64 maskBits = PhysicsLayers::All) const;

    // Contact/sensor begin/end events collected during update() (this frame's steps), reported as the
    // colliding bodies' userData pointers (Entity* for component-spawned bodies, null otherwise).
    // For sensor events userDataA is the sensor's body.
    struct ContactEvent
    {
        void* userDataA = nullptr;
        void* userDataB = nullptr;
        bool begin = false;  // begin or end touch
        bool sensor = false; // sensor overlap instead of a solid contact
    };
    std::span<const ContactEvent> getContactEvents() const { return m_contactEvents; }

    void setGravity(const glm::vec3& gravity);

    // Fixed-step render interpolation: total steps taken, and the fraction [0,1] of the current frame
    // into the next step (1 when interpolation is disabled via the Tweak).
    uint32 getStepCount() const { return m_stepCount; }
    float getInterpolationAlpha() const;

    bool isInitialized() const { return m_initialized; }

private:

    uint32 m_worldHandle = 0; // b3WorldId bits
    bool m_initialized = false;
    bool m_paused = false;
    bool m_interpolate = true;
    float m_accumulator = 0.0f;
    float m_timeScale = 1.0f;
    int m_subSteps = 4;
    int m_stepHz = 20;
    uint32 m_stepCount = 0;
    glm::vec3 m_gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    PhysicsBody m_staticBody;
    std::vector<ContactEvent> m_contactEvents;
};

export namespace Globals
{
    PhysicsWorld physics;
}
