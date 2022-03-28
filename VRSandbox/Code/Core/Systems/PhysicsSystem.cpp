#include "PhysicsSystem.h"

#include "Components/PhysicsComponents.h"
#include "Components/SceneComponent.h"
#include "Utils/PhysicsMotionState.h"

#include <btBulletDynamicsCommon.h>
#include <OgreAssert.h>
#include <OgreVector3.h>
#include <OgreSceneNode.h>
#include <entt/entity/registry.hpp>

PhysicsSystem::PhysicsSystem() 
{
	m_pCollisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>();
	m_pDispatcher = std::make_unique<btCollisionDispatcher>(m_pCollisionConfiguration.get());
	m_pOverlappingPairCache = std::make_unique<btDbvtBroadphase>();
	m_pSolver = std::make_unique<btSequentialImpulseConstraintSolver>();
	m_pDynamicsWorld = std::make_unique<btDiscreteDynamicsWorld>(m_pDispatcher.get(), m_pOverlappingPairCache.get(), m_pSolver.get(), m_pCollisionConfiguration.get());
	m_pDynamicsWorld->setGravity(btVector3(0, -9.81, 0));
}

PhysicsSystem::~PhysicsSystem() 
{
	OGRE_ASSERT(m_pDynamicsWorld->getNumCollisionObjects() == 0);

	m_pDynamicsWorld.release();
	m_pSolver.release();
	m_pOverlappingPairCache.release();
	m_pDispatcher.release();
	m_pCollisionConfiguration.release();
}

void PhysicsSystem::update(double deltaSec, double fixedTimestep, entt::registry& registry)
{
	m_pDynamicsWorld->stepSimulation(btScalar(deltaSec), 1, btScalar(fixedTimestep));

	auto view = registry.view<PhysicsComponent, SceneComponent>();
	view.each([](PhysicsComponent& physComp, SceneComponent& nodeComp)
		{
			auto& trans = physComp.pBody->getWorldTransform();
			auto& physPos = trans.getOrigin();
			auto& physRot = trans.getRotation();
			nodeComp.pNode->setPosition(*reinterpret_cast<Ogre::Vector3*>(&physPos));
			nodeComp.pNode->setOrientation(Ogre::Quaternion(physRot.w(), physRot.x(), physRot.y(), physRot.z()));
		});
}

btCollisionShape* PhysicsSystem::createBoxShape(Ogre::Vector3 dimensions)
{
	//TODO: reuse shapes if dimensions are same
	btCollisionShape* shape = new btBoxShape(btVector3(btScalar(dimensions.x), btScalar(dimensions.y), btScalar(dimensions.z)));
	shape->setUserIndex(1);
	m_shapes.emplace_back(shape);
	return shape;
}

btCollisionShape* PhysicsSystem::createSphereShape(Ogre::Real radius)
{
	btCollisionShape* shape = new btSphereShape(btScalar(radius));
	shape->setUserIndex(1);
	m_shapes.emplace_back(shape);
	return shape;
}

void PhysicsSystem::destroyShape(btCollisionShape* pShape)
{
	OGRE_ASSERT(pShape->getUserIndex() == 1);
	auto it = std::find(m_shapes.begin(), m_shapes.end(), pShape);
	OGRE_ASSERT(it != m_shapes.end());
	m_shapes.erase(it);
}

PhysicsComponent& PhysicsSystem::addPhysicsBodyComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape,
	const Ogre::Vector3& position, const Ogre::Quaternion& orientation, float mass)
{
	OGRE_ASSERT(registry.try_get<SceneComponent>(entity));
	SceneComponent& sceneComponent = registry.get<SceneComponent>(entity);
	PhysicsComponent& component = registry.emplace<PhysicsComponent>(entity);
	sceneComponent.pNode->setPosition(position);
	sceneComponent.pNode->setOrientation(orientation);

	btVector3 localInertia(0, 0, 0);
	if (pShape && mass >= 0.0f)
		pShape->calculateLocalInertia(mass, localInertia);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, new PhysicsMotionState(entity, registry), pShape, localInertia);

	component.pBody = new btRigidBody(rbInfo);

	m_pDynamicsWorld->addRigidBody(component.pBody);
	return component;
}

void PhysicsSystem::removePhysicsBodyComponent(entt::registry& registry, entt::entity entity)
{
	const auto& component = registry.get<PhysicsComponent>(entity);
	btRigidBody* pBody = component.pBody;
	OGRE_ASSERT(pBody);

	btMotionState* pMotionState = pBody->getMotionState();
	OGRE_ASSERT(pMotionState);
	delete pMotionState;

	const auto shape = pBody->getCollisionShape();
	const int userCount = shape->getUserIndex();
	OGRE_ASSERT(userCount >= 1);
	if (userCount <= 1)
		destroyShape(shape);
	else
		shape->setUserIndex(userCount - 1);
	m_pDynamicsWorld->removeRigidBody(pBody);
	delete pBody;

	size_t numRemoved = registry.remove<PhysicsComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}