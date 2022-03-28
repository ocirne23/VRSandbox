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
struct PhysicsComponent;

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
	PhysicsComponent& addPhysicsBodyComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape, const Ogre::Vector3& position, const Ogre::Quaternion& orientation, float mass);
	void removePhysicsBodyComponent(entt::registry & registry, entt::entity entity);

	btDiscreteDynamicsWorld* getWorld() const { return m_pDynamicsWorld.get(); }

private:

	std::vector<btCollisionShape*> m_shapes;

	std::unique_ptr<btDefaultCollisionConfiguration> m_pCollisionConfiguration;
	std::unique_ptr<btCollisionDispatcher> m_pDispatcher;
	std::unique_ptr<btBroadphaseInterface> m_pOverlappingPairCache;
	std::unique_ptr<btSequentialImpulseConstraintSolver> m_pSolver;
	std::unique_ptr<btDiscreteDynamicsWorld> m_pDynamicsWorld;
};