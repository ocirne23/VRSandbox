module;

#include <box3d/box3d.h>

module Physics;

import Core;
import Core.glm;
import Core.Log;
import Core.Tweaks;

static_assert(sizeof(b3BodyId) == sizeof(uint64));
static_assert(sizeof(b3WorldId) == sizeof(uint32));

static b3Vec3 toB3(const glm::vec3& v) { return b3Vec3{ v.x, v.y, v.z }; }
static b3Quat toB3(const glm::quat& q) { return b3Quat{ { q.x, q.y, q.z }, q.w }; }
static glm::vec3 toGlm(const b3Vec3& v) { return glm::vec3(v.x, v.y, v.z); }
static glm::quat toGlm(const b3Quat& q) { return glm::quat(q.s, q.v.x, q.v.y, q.v.z); }

static b3BodyId toBodyId(uint64 handle) { return std::bit_cast<b3BodyId>(handle); }

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

bool PhysicsWorld::initialize()
{
    if (m_initialized)
        return true;

    b3WorldDef def = b3DefaultWorldDef();
    def.gravity = toB3(m_gravity);
    const b3WorldId world = b3CreateWorld(&def);
    if (B3_IS_NULL(world))
    {
        Log::warning("Physics: failed to create box3d world");
        return false;
    }
    m_worldHandle = std::bit_cast<uint32>(world);
    m_initialized = true;

    Tweak::float3("Physics/World", "Gravity", &m_gravity, 0.05f, [this] { setGravity(m_gravity); });
    Tweak::boolean("Physics/World", "Paused", &m_paused);
    Tweak::floatVar("Physics/World", "Time Scale", &m_timeScale, 0.0f, 4.0f);
    Tweak::intVar("Physics/World", "Sub Steps", &m_subSteps, 1, 16);
    Tweak::intVar("Physics/World", "Step Hz", &m_stepHz, 30, 240);
    return true;
}

void PhysicsWorld::shutdown()
{
    if (!m_initialized)
        return;
    b3DestroyWorld(std::bit_cast<b3WorldId>(m_worldHandle));
    m_worldHandle = 0;
    m_initialized = false;
}

void PhysicsWorld::update(double deltaSec)
{
    if (!m_initialized || m_paused)
        return;

    m_accumulator += float(deltaSec) * m_timeScale;
    const float step = 1.0f / float(m_stepHz);
    const b3WorldId world = std::bit_cast<b3WorldId>(m_worldHandle);

    constexpr int maxCatchUpSteps = 4;
    int steps = 0;
    while (m_accumulator >= step && steps < maxCatchUpSteps)
    {
        b3World_Step(world, step, m_subSteps);
        m_accumulator -= step;
        ++steps;
    }
    if (m_accumulator >= step)
        m_accumulator = 0.0f; // drop the remainder after a hitch instead of spiraling
}

PhysicsBody PhysicsWorld::createBody(const PhysicsBodyDesc& desc, std::span<const PhysicsShape> shapes)
{
    assert(m_initialized);

    b3BodyDef bodyDef = b3DefaultBodyDef();
    switch (desc.type)
    {
    case EPhysicsBodyType::Static:    bodyDef.type = b3_staticBody;    break;
    case EPhysicsBodyType::Kinematic: bodyDef.type = b3_kinematicBody; break;
    case EPhysicsBodyType::Dynamic:   bodyDef.type = b3_dynamicBody;   break;
    }
    bodyDef.position = toB3(desc.transform.pos);
    bodyDef.rotation = toB3(glm::normalize(desc.transform.quat));
    bodyDef.linearVelocity = toB3(desc.linearVelocity);
    bodyDef.userData = desc.userData;

    const b3BodyId body = b3CreateBody(std::bit_cast<b3WorldId>(m_worldHandle), &bodyDef);
    if (B3_IS_NULL(body))
        return PhysicsBody();

    const float scale = desc.transform.scale;
    for (const PhysicsShape& shape : shapes)
    {
        b3ShapeDef shapeDef = b3DefaultShapeDef();
        shapeDef.density = shape.density;
        shapeDef.baseMaterial.friction = shape.friction;
        shapeDef.baseMaterial.restitution = shape.restitution;

        const glm::vec3 offset = shape.offset * scale;
        switch (shape.type)
        {
        case EPhysicsShapeType::Box:
        {
            const glm::vec3 he = shape.halfExtents * scale;
            const b3BoxHull hull = b3MakeOffsetBoxHull(he.x, he.y, he.z, toB3(offset));
            b3CreateHullShape(body, &shapeDef, &hull.base);
            break;
        }
        case EPhysicsShapeType::Sphere:
        {
            const b3Sphere sphere{ toB3(offset), shape.radius * scale };
            b3CreateSphereShape(body, &shapeDef, &sphere);
            break;
        }
        case EPhysicsShapeType::Capsule:
        {
            const glm::vec3 axis(0.0f, shape.halfHeight * scale, 0.0f);
            const b3Capsule capsule{ toB3(offset - axis), toB3(offset + axis), shape.radius * scale };
            b3CreateCapsuleShape(body, &shapeDef, &capsule);
            break;
        }
        }
    }
    return PhysicsBody(std::bit_cast<uint64>(body));
}

PhysicsWorld::RayHit PhysicsWorld::castRayClosest(const glm::vec3& origin, const glm::vec3& translation) const
{
    RayHit outHit;
    if (!m_initialized)
        return outHit;
    const b3RayResult result = b3World_CastRayClosest(std::bit_cast<b3WorldId>(m_worldHandle),
        toB3(origin), toB3(translation), b3DefaultQueryFilter());
    outHit.hit = result.hit;
    outHit.fraction = result.fraction;
    outHit.point = toGlm(result.point);
    outHit.normal = toGlm(result.normal);
    return outHit;
}

void PhysicsWorld::setGravity(const glm::vec3& gravity)
{
    m_gravity = gravity;
    if (m_initialized)
        b3World_SetGravity(std::bit_cast<b3WorldId>(m_worldHandle), toB3(m_gravity));
}
