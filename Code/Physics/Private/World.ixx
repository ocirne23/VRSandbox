export module Physics:World;

import Core;
import Core.glm;

import :Body;
import :Joint;
import :Mesh;
import :Types;
import :Layers;

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
        bool begin = false;   // begin or end touch
        bool sensor = false;  // sensor overlap instead of a solid contact
        int64 contactId = 0;  // opaque box3d contact handle (0 for sensor events, which have no manifold);
                               // pass to getContactPoint() before the next update() call, after which it may
                               // reference a recycled contact
    };
    std::span<const ContactEvent> getContactEvents() const { return m_contactEvents; }

    // Resolves a ContactEvent::contactId (from THIS frame, before the next update()) to its first manifold
    // point in world space. Returns false (leaving the outputs untouched) if the contact is stale/gone or
    // has no manifold point (e.g. purely speculative contact).
    bool getContactPoint(int64 contactId, glm::vec3& outPoint, glm::vec3& outNormal) const;

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
