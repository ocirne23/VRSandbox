module;

#include <box3d/box3d.h>

module Physics;

import Core;
import Core.glm;
import Core.Log;
import Core.Tweaks;

static_assert(sizeof(b3BodyId) == sizeof(uint64));
static_assert(sizeof(b3JointId) == sizeof(uint64));
static_assert(sizeof(b3WorldId) == sizeof(uint32));
static_assert(sizeof(b3Vec3) == sizeof(glm::vec3));

static b3Vec3 toB3(const glm::vec3& v) { return b3Vec3{ v.x, v.y, v.z }; }
static b3Quat toB3(const glm::quat& q) { return b3Quat{ { q.x, q.y, q.z }, q.w }; }
static glm::vec3 toGlm(const b3Vec3& v) { return glm::vec3(v.x, v.y, v.z); }
static glm::quat toGlm(const b3Quat& q) { return glm::quat(q.s, q.v.x, q.v.y, q.v.z); }

static b3BodyId toBodyId(uint64 handle) { return std::bit_cast<b3BodyId>(handle); }

// b3ContactId is 3 fields (index1/world0/generation) and doesn't fit a single bit_cast-able integer; world0
// is always this process's one physics world, so it's dropped here and re-supplied from m_worldHandle when
// unpacking (see PhysicsWorld::getContactPoint). Packing is lossless for index1/generation, so this is valid
// for as long as box3d itself guarantees the id (until the next world step recycles it).
static int64 packContactId(b3ContactId id)
{
    return (int64)(((uint64)(uint32)id.index1 << 32) | (uint64)id.generation);
}

// Layer name -> category bit registry; index in the vector = bit index. Session-local: bits are only
// compared against each other at runtime, so allocation order between runs doesn't matter.
static std::vector<std::string>& layerNames()
{
    static std::vector<std::string> names = { "Default" };
    return names;
}

uint64 PhysicsLayers::bit(std::string_view name)
{
    std::vector<std::string>& names = layerNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name)
            return 1ull << i;
    if (names.size() >= 64)
    {
        Log::error("Physics: out of collision layer bits, '" + std::string(name) + "' falls back to Default");
        return 1ull;
    }
    names.emplace_back(name);
    return 1ull << (names.size() - 1);
}

void PhysicsMesh::destroy()
{
    if (m_data == 0)
        return;
    b3DestroyMesh(reinterpret_cast<b3MeshData*>(m_data));
    m_data = 0;
}

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

void PhysicsJoint::destroy()
{
    if (m_handle == 0)
        return;
    const b3JointId id = std::bit_cast<b3JointId>(m_handle);
    if (b3Joint_IsValid(id)) // destroying an attached body already destroyed the joint
        b3DestroyJoint(id, true);
    m_handle = 0;
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

    PhysicsBodyDesc staticDesc;
    staticDesc.type = EPhysicsBodyType::Static;
    m_staticBody = createBody(staticDesc, {});

    Tweak::float3("Physics/World", "Gravity", &m_gravity, 0.05f, [this] { setGravity(m_gravity); });
    Tweak::boolean("Physics/World", "Paused", &m_paused);
    Tweak::boolean("Physics/World", "Interpolate", &m_interpolate);
    Tweak::floatVar("Physics/World", "Time Scale", &m_timeScale, 0.0f, 4.0f);
    Tweak::intVar("Physics/World", "Sub Steps", &m_subSteps, 1, 16);
    Tweak::intVar("Physics/World", "Step Hz", &m_stepHz, 5, 120);
    return true;
}

void PhysicsWorld::shutdown()
{
    if (!m_initialized)
        return;
    m_staticBody.destroy();
    b3DestroyWorld(std::bit_cast<b3WorldId>(m_worldHandle));
    m_worldHandle = 0;
    m_initialized = false;
}

// Appends the step's buffered contact/sensor begin/end events (box3d clears them on the next step,
// so they are drained after every step, not once per frame). End events may reference destroyed
// shapes and are validated first.
void appendContactEvents(b3WorldId world, std::vector<PhysicsWorld::ContactEvent>& outEvents)
{
    auto userDataOf = [](b3ShapeId shape) { return b3Body_GetUserData(b3Shape_GetBody(shape)); };

    const b3ContactEvents contacts = b3World_GetContactEvents(world);
    for (int i = 0; i < contacts.beginCount; ++i)
    {
        const b3ContactBeginTouchEvent& e = contacts.beginEvents[i];
        outEvents.push_back({ userDataOf(e.shapeIdA), userDataOf(e.shapeIdB), true, false, packContactId(e.contactId) });
    }
    for (int i = 0; i < contacts.endCount; ++i)
    {
        const b3ContactEndTouchEvent& e = contacts.endEvents[i];
        if (!b3Shape_IsValid(e.shapeIdA) || !b3Shape_IsValid(e.shapeIdB))
            continue;
        outEvents.push_back({ userDataOf(e.shapeIdA), userDataOf(e.shapeIdB), false, false, packContactId(e.contactId) });
    }

    const b3SensorEvents sensors = b3World_GetSensorEvents(world);
    for (int i = 0; i < sensors.beginCount; ++i)
    {
        const b3SensorBeginTouchEvent& e = sensors.beginEvents[i];
        outEvents.push_back({ userDataOf(e.sensorShapeId), userDataOf(e.visitorShapeId), true, true, 0 });
    }
    for (int i = 0; i < sensors.endCount; ++i)
    {
        const b3SensorEndTouchEvent& e = sensors.endEvents[i];
        if (!b3Shape_IsValid(e.sensorShapeId) || !b3Shape_IsValid(e.visitorShapeId))
            continue;
        outEvents.push_back({ userDataOf(e.sensorShapeId), userDataOf(e.visitorShapeId), false, true, 0 });
    }
}

void PhysicsWorld::update(double deltaSec)
{
    m_contactEvents.clear();
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
        ++m_stepCount;
        appendContactEvents(world, m_contactEvents);
        m_accumulator -= step;
        ++steps;
    }
    if (m_accumulator >= step)
        m_accumulator = 0.0f; // drop the remainder after a hitch instead of spiraling
}

float PhysicsWorld::getInterpolationAlpha() const
{
    if (!m_interpolate)
        return 1.0f;
    return glm::clamp(m_accumulator * float(m_stepHz), 0.0f, 1.0f);
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
        shapeDef.filter.categoryBits = shape.categoryBits;
        shapeDef.filter.maskBits = shape.maskBits;
        shapeDef.filter.groupIndex = shape.groupIndex;
        shapeDef.isSensor = shape.isSensor;
        shapeDef.enableSensorEvents = true; // visitors must opt in for sensors to see them; always on so sensors just work
        shapeDef.enableContactEvents = shape.contactEvents;

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
        case EPhysicsShapeType::Hull:
        {
            if (shape.hullPoints.size() < 4)
            {
                Log::warning("Physics: hull shape needs at least 4 points");
                break;
            }
            std::vector<b3Vec3> points;
            points.reserve(shape.hullPoints.size());
            for (const glm::vec3& p : shape.hullPoints)
                points.push_back(toB3(p * scale + offset));
            if (b3HullData* hull = b3CreateHull(points.data(), int(points.size()), glm::clamp(shape.maxHullVertices, 4, 64)))
            {
                b3CreateHullShape(body, &shapeDef, hull); // the shape clones the hull
                b3DestroyHull(hull);
            }
            else
                Log::warning("Physics: convex hull creation failed (degenerate point cloud?)");
            break;
        }
        case EPhysicsShapeType::Mesh:
        {
            if (!shape.mesh || !shape.mesh->isValid())
            {
                Log::warning("Physics: mesh shape without collision mesh data");
                break;
            }
            if (desc.type != EPhysicsBodyType::Static)
                Log::warning("Physics: mesh colliders only generate contacts on static bodies");
            const b3MeshData* meshData = reinterpret_cast<const b3MeshData*>(shape.mesh->m_data);
            b3CreateMeshShape(body, &shapeDef, meshData, b3Vec3{ scale, scale, scale });
            break;
        }
        }
    }
    return PhysicsBody(std::bit_cast<uint64>(body));
}

PhysicsMesh PhysicsWorld::createCollisionMesh(std::span<const glm::vec3> vertices, std::span<const uint32> indices)
{
    PhysicsMesh outMesh;
    if (vertices.size() < 3 || indices.size() < 3)
        return outMesh;

    b3MeshDef def{}; // the input arrays are only read during creation
    def.vertices = reinterpret_cast<b3Vec3*>(const_cast<glm::vec3*>(vertices.data()));
    def.indices = reinterpret_cast<int32_t*>(const_cast<uint32*>(indices.data()));
    def.vertexCount = int(vertices.size());
    def.triangleCount = int(indices.size() / 3);
    def.weldVertices = true;
    def.weldTolerance = 0.001f;
    def.identifyEdges = true; // smooth contacts across internal edges

    b3MeshData* data = b3CreateMesh(&def, nullptr, 0);
    if (!data)
    {
        Log::warning("Physics: collision mesh creation failed");
        return outMesh;
    }
    outMesh.m_data = reinterpret_cast<uint64>(data);
    return outMesh;
}

// Quaternion rotating the +Z axis (box3d's hinge/cone axis convention) onto `axis`.
static glm::quat quatFromZAxis(glm::vec3 axis)
{
    axis = glm::normalize(axis);
    const glm::vec3 z(0.0f, 0.0f, 1.0f);
    const float d = glm::dot(z, axis);
    if (d > 0.9999f)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.9999f)
        return glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 c = glm::cross(z, axis);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

// World-space joint frame expressed in the body's local space.
static b3Transform localJointFrame(const PhysicsBody& body, const glm::vec3& worldAnchor, const glm::quat& worldRot)
{
    const glm::quat invRot = glm::conjugate(body.getRotation());
    return b3Transform{ toB3(invRot * (worldAnchor - body.getPosition())), toB3(glm::normalize(invRot * worldRot)) };
}

static void fillJointBase(b3JointDef& base, const PhysicsBody& a, const PhysicsBody& b, uint64 handleA, uint64 handleB,
    const glm::vec3& anchorA, const glm::vec3& anchorB, const glm::quat& worldRot)
{
    base.bodyIdA = toBodyId(handleA);
    base.bodyIdB = toBodyId(handleB);
    base.localFrameA = localJointFrame(a, anchorA, worldRot);
    base.localFrameB = localJointFrame(b, anchorB, worldRot);
    base.collideConnected = false;
}

PhysicsJoint PhysicsWorld::createDistanceJoint(const PhysicsBody& a, const PhysicsBody& b,
    const glm::vec3& anchorA, const glm::vec3& anchorB, float minLength, float maxLength)
{
    assert(m_initialized && a.isValid() && b.isValid());
    b3DistanceJointDef def = b3DefaultDistanceJointDef();
    fillJointBase(def.base, a, b, a.m_handle, b.m_handle, anchorA, anchorB, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    def.length = glm::distance(anchorA, anchorB);
    def.enableLimit = true;
    def.minLength = glm::min(minLength, maxLength);
    def.maxLength = glm::max(minLength, maxLength);
    return PhysicsJoint(std::bit_cast<uint64>(b3CreateDistanceJoint(std::bit_cast<b3WorldId>(m_worldHandle), &def)));
}

PhysicsJoint PhysicsWorld::createRevoluteJoint(const PhysicsBody& a, const PhysicsBody& b,
    const glm::vec3& pivot, const glm::vec3& axis, float lowerDeg, float upperDeg)
{
    assert(m_initialized && a.isValid() && b.isValid());
    b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
    fillJointBase(def.base, a, b, a.m_handle, b.m_handle, pivot, pivot, quatFromZAxis(axis));
    if (lowerDeg > -FLT_MAX * 0.5f || upperDeg < FLT_MAX * 0.5f)
    {
        def.enableLimit = true;
        def.lowerAngle = glm::radians(glm::max(lowerDeg, -178.0f));
        def.upperAngle = glm::radians(glm::min(upperDeg, 178.0f));
    }
    return PhysicsJoint(std::bit_cast<uint64>(b3CreateRevoluteJoint(std::bit_cast<b3WorldId>(m_worldHandle), &def)));
}

PhysicsJoint PhysicsWorld::createSphericalJoint(const PhysicsBody& a, const PhysicsBody& b,
    const glm::vec3& pivot, float coneAngleDeg)
{
    assert(m_initialized && a.isValid() && b.isValid());
    b3SphericalJointDef def = b3DefaultSphericalJointDef();
    fillJointBase(def.base, a, b, a.m_handle, b.m_handle, pivot, pivot, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    if (coneAngleDeg < 180.0f)
    {
        def.enableConeLimit = true;
        def.coneAngle = glm::radians(glm::clamp(coneAngleDeg, 0.0f, 180.0f));
    }
    return PhysicsJoint(std::bit_cast<uint64>(b3CreateSphericalJoint(std::bit_cast<b3WorldId>(m_worldHandle), &def)));
}

PhysicsJoint PhysicsWorld::createWeldJoint(const PhysicsBody& a, const PhysicsBody& b, const glm::vec3& anchor)
{
    assert(m_initialized && a.isValid() && b.isValid());
    b3WeldJointDef def = b3DefaultWeldJointDef();
    fillJointBase(def.base, a, b, a.m_handle, b.m_handle, anchor, anchor, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    return PhysicsJoint(std::bit_cast<uint64>(b3CreateWeldJoint(std::bit_cast<b3WorldId>(m_worldHandle), &def)));
}

PhysicsWorld::RayHit PhysicsWorld::castRayClosest(const glm::vec3& origin, const glm::vec3& translation, uint64 maskBits) const
{
    RayHit outHit;
    if (!m_initialized)
        return outHit;
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.categoryBits = PhysicsLayers::All; // pass every shape's own mask; maskBits alone decides what the ray sees
    filter.maskBits = maskBits;
    const b3RayResult result = b3World_CastRayClosest(std::bit_cast<b3WorldId>(m_worldHandle),
        toB3(origin), toB3(translation), filter);
    outHit.hit = result.hit;
    outHit.fraction = result.fraction;
    outHit.point = toGlm(result.point);
    outHit.normal = toGlm(result.normal);
    return outHit;
}

bool PhysicsWorld::getContactPoint(int64 contactId, glm::vec3& outPoint, glm::vec3& outNormal) const
{
    if (!m_initialized || contactId == 0)
        return false;

    b3ContactId id{};
    id.index1 = (int32)((uint64)contactId >> 32);
    id.world0 = std::bit_cast<b3WorldId>(m_worldHandle).index1;
    id.padding = 0;
    id.generation = (uint32)((uint64)contactId & 0xffffffffu);
    if (!b3Contact_IsValid(id))
        return false;

    const b3ContactData data = b3Contact_GetData(id);
    if (data.manifoldCount == 0 || data.manifolds[0].pointCount == 0)
        return false;

    const b3Manifold& manifold = data.manifolds[0];
    const glm::vec3 comA = toGlm(b3Body_GetWorldCenterOfMass(b3Shape_GetBody(data.shapeIdA)));
    outPoint = comA + toGlm(manifold.points[0].anchorA);
    outNormal = toGlm(manifold.normal);
    return true;
}

void PhysicsWorld::setGravity(const glm::vec3& gravity)
{
    m_gravity = gravity;
    if (m_initialized)
        b3World_SetGravity(std::bit_cast<b3WorldId>(m_worldHandle), toB3(m_gravity));
}
