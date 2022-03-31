#pragma once

#include <memory>
#include <vector>
#include <entt/fwd.hpp>
#include <OgreVector3.h>
#include <OgreQuaternion.h>

class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btRigidBody;
class btCollisionShape;
struct DynamicPhysicsComponent;
struct StaticPhysicsComponent;
struct KinematicPhysicsComponent;
struct SpringJointComponent;

class PhysicsSystem
{
public:

	PhysicsSystem();
	virtual ~PhysicsSystem();
	PhysicsSystem(const PhysicsSystem& copy) = delete;

	void update(double deltaSec, double fixedTimestep, entt::registry& registry);

	btCollisionShape* createBoxShape(Ogre::Vector3 dimensions);
	btCollisionShape* createSphereShape(Ogre::Real radius);
	void destroyShape(btCollisionShape* pShape);

	DynamicPhysicsComponent& addDynamicPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape, float mass);
	StaticPhysicsComponent& addStaticPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape);
	KinematicPhysicsComponent& addKinematicPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape);

	SpringJointComponent& addSpringJointComponent(entt::registry& registry, entt::entity entity, btRigidBody* pBody1, btRigidBody* pBody2);

	btDiscreteDynamicsWorld* getWorld() const { return m_pDynamicsWorld.get(); }

private:

	btRigidBody* createRigidBody(entt::registry& registry, entt::entity entity, btCollisionShape* pShape, float mass);
	void destroyRigidBody(btRigidBody* pBody);

private:

	std::vector<btCollisionShape*> m_shapes;

	std::unique_ptr<btDefaultCollisionConfiguration> m_pCollisionConfiguration;
	std::unique_ptr<btCollisionDispatcher> m_pDispatcher;
	std::unique_ptr<btBroadphaseInterface> m_pOverlappingPairCache;
	std::unique_ptr<btSequentialImpulseConstraintSolver> m_pSolver;
	std::unique_ptr<btDiscreteDynamicsWorld> m_pDynamicsWorld;
};