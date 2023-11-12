module;

#include <vector>
#include <entt/fwd.hpp>
#include <OgreVector3.h>
#include <OgreQuaternion.h>

export module Systems.PhysicsSystem;

import <memory>;

export class World;
export class btDefaultCollisionConfiguration;
export class btCollisionDispatcher;
export class btBroadphaseInterface;
export class btSequentialImpulseConstraintSolver;
export class btDiscreteDynamicsWorld;
export class btRigidBody;
export class btCollisionShape;
export class btTriangleMesh;
export class DebugDrawer;
export class PhysicsDebugDrawer;
export struct DynamicPhysicsComponent;
export struct StaticPhysicsComponent;
export struct KinematicPhysicsComponent;
export struct SpringJointComponent;
export struct FixedJointComponent;

import Utils.Owner;

export class PhysicsSystem
{
public:

	PhysicsSystem(World& world, entt::registry& registry);
	virtual ~PhysicsSystem();
	PhysicsSystem(const PhysicsSystem& copy) = delete;

	void initialize();

	void update(double deltaSec, double fixedTimestep);
	void setDebugDrawer(DebugDrawer* pDebugDrawer);
	void setEnableDebugDraw(bool enable);

	btCollisionShape* createTriangleShapeFromOgreMesh(Ogre::String meshName,
		Ogre::Vector3 scale = Ogre::Vector3(1, 1, 1),
		Ogre::Vector3 position = Ogre::Vector3(0, 0, 0),
		Ogre::Quaternion orient = Ogre::Quaternion::IDENTITY);

	btCollisionShape* createBoxShape(const Ogre::Vector3& dimensions);
	btCollisionShape* createSphereShape(Ogre::Real radius);
	btCollisionShape* createConeShape(Ogre::Real radius, Ogre::Real height);
	btCollisionShape* createCapsuleShape(Ogre::Real radius, Ogre::Real height);
	void destroyShape(btCollisionShape* pShape);

	DynamicPhysicsComponent& addDynamicPhysicsComponent(entt::entity entity, btCollisionShape* pShape, float mass);
	StaticPhysicsComponent& addStaticPhysicsComponent(entt::entity entity, btCollisionShape* pShape);
	KinematicPhysicsComponent& addKinematicPhysicsComponent(entt::entity entity, btCollisionShape* pShape);
	SpringJointComponent& addSpringJointComponent(entt::entity entity, 
		btRigidBody* pBody1, btRigidBody* pBody2, float stiffness = 1000.0f, float damping = 1.0f,
		const Ogre::Vector3& attach1 = Ogre::Vector3(0, 0, 0), const Ogre::Vector3& attach2 = Ogre::Vector3(0, 0, 0), 
		const Ogre::Vector3& limitMin = Ogre::Vector3(-10, -10, -10), const Ogre::Vector3& limitMax = Ogre::Vector3(10, 10, 10));
	FixedJointComponent& addFixedJointComponent(entt::entity entity, btRigidBody* pBody1, btRigidBody* pBody2, const Ogre::Vector3& attach1 = Ogre::Vector3(), const Ogre::Vector3& attach2 = Ogre::Vector3());

	void removeDynamicPhysicsComponent(entt::entity entity);
	void removeStaticPhysicsComponent(entt::entity entity);
	void removeKinematicPhysicsComponent(entt::entity entity);
	void removeSpringJointComponent(entt::entity entity);
	void removeFixedJointComponent(entt::entity entity);

	btDiscreteDynamicsWorld* getWorld() const { return m_pDynamicsWorld.get(); }

private:

	btRigidBody* createRigidBody(entt::entity entity, btCollisionShape* pShape, float mass);
	void destroyRigidBody(btRigidBody* pBody);

private:

	World& m_world;
	entt::registry& m_registry;

	double m_timeAccumulator = 0.0;

	std::vector<gsl::owner<btCollisionShape*>> m_shapes;
	std::unordered_map<Ogre::String, std::pair<gsl::owner<btCollisionShape*>, gsl::owner<btTriangleMesh*>>> m_triangleShapeMap;
	std::vector<gsl::owner<btRigidBody*>> m_bodies;

	std::unique_ptr<btDefaultCollisionConfiguration> m_pCollisionConfiguration;
	std::unique_ptr<btCollisionDispatcher> m_pDispatcher;
	std::unique_ptr<btBroadphaseInterface> m_pOverlappingPairCache;
	std::unique_ptr<btSequentialImpulseConstraintSolver> m_pSolver;
	std::unique_ptr<btDiscreteDynamicsWorld> m_pDynamicsWorld;
	std::unique_ptr<PhysicsDebugDrawer> m_pPhysicsDebugDrawer;
};