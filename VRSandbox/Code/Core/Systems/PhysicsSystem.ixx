module;

#include <memory>
#include <vector>
#include <entt/fwd.hpp>
#include <OgreVector3.h>
#include <OgreQuaternion.h>

export module Systems.PhysicsSystem;

export class btDefaultCollisionConfiguration;
export class btCollisionDispatcher;
export class btBroadphaseInterface;
export class btSequentialImpulseConstraintSolver;
export class btDiscreteDynamicsWorld;
export class btRigidBody;
export class btCollisionShape;
export class DebugDrawer;
export class PhysicsDebugDrawer;
export struct DynamicPhysicsComponent;
export struct StaticPhysicsComponent;
export struct KinematicPhysicsComponent;
export struct SpringJointComponent;
export struct FixedJointComponent;

export class PhysicsSystem
{
public:

	PhysicsSystem();
	virtual ~PhysicsSystem();
	PhysicsSystem(const PhysicsSystem& copy) = delete;

	void update(double deltaSec, double fixedTimestep, entt::registry& registry);
	void setDebugDrawer(DebugDrawer* pDebugDrawer);
	void setEnableDebugDraw(bool enable);

	btCollisionShape* createTriangleShapeFromOgreMesh(Ogre::String meshName);
	btCollisionShape* createBoxShape(const Ogre::Vector3& dimensions);
	btCollisionShape* createSphereShape(Ogre::Real radius);
	btCollisionShape* createConeShape(Ogre::Real radius, Ogre::Real height);
	btCollisionShape* createCapsuleShape(Ogre::Real radius, Ogre::Real height);
	void destroyShape(btCollisionShape* pShape);

	DynamicPhysicsComponent& addDynamicPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape, float mass);
	StaticPhysicsComponent& addStaticPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape);
	KinematicPhysicsComponent& addKinematicPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape);
	SpringJointComponent& addSpringJointComponent(entt::registry& registry, entt::entity entity, 
		btRigidBody* pBody1, btRigidBody* pBody2, float stiffness = 1000.0f, float damping = 1.0f,
		const Ogre::Vector3& attach1 = Ogre::Vector3(0, 0, 0), const Ogre::Vector3& attach2 = Ogre::Vector3(0, 0, 0), 
		const Ogre::Vector3& limitMin = Ogre::Vector3(-10, -10, -10), const Ogre::Vector3& limitMax = Ogre::Vector3(10, 10, 10));
	FixedJointComponent& addFixedJointComponent(entt::registry& registry, entt::entity entity, btRigidBody* pBody1, btRigidBody* pBody2, const Ogre::Vector3& attach1 = Ogre::Vector3(), const Ogre::Vector3& attach2 = Ogre::Vector3());

	void removeDynamicPhysicsComponent(entt::registry& registry, entt::entity entity);
	void removeStaticPhysicsComponent(entt::registry& registry, entt::entity entity);
	void removeKinematicPhysicsComponent(entt::registry& registry, entt::entity entity);
	void removeSpringJointComponent(entt::registry& registry, entt::entity entity);
	void removeFixedJointComponent(entt::registry& registry, entt::entity entity);

	btDiscreteDynamicsWorld* getWorld() const { return m_pDynamicsWorld.get(); }

private:

	btRigidBody* createRigidBody(entt::registry& registry, entt::entity entity, btCollisionShape* pShape, float mass);
	void destroyRigidBody(btRigidBody* pBody);

private:

	std::vector<btCollisionShape*> m_shapes;
	std::unordered_map<Ogre::String, btCollisionShape*> m_triangleShapeMap;

	std::unique_ptr<btDefaultCollisionConfiguration> m_pCollisionConfiguration;
	std::unique_ptr<btCollisionDispatcher> m_pDispatcher;
	std::unique_ptr<btBroadphaseInterface> m_pOverlappingPairCache;
	std::unique_ptr<btSequentialImpulseConstraintSolver> m_pSolver;
	std::unique_ptr<btDiscreteDynamicsWorld> m_pDynamicsWorld;
	std::unique_ptr<PhysicsDebugDrawer> m_pPhysicsDebugDrawer;
};