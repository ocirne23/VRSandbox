module;

#include <btBulletDynamicsCommon.h>
#include <OgreAssert.h>
#include <OgreVector3.h>
#include <OgreSceneNode.h>
#include <entt/entity/registry.hpp>

module Systems.PhysicsSystem;

import Components.DynamicPhysicsComponent;
import Components.StaticPhysicsComponent;
import Components.KinematicPhysicsComponent;
import Components.SceneComponent;
import Components.SpringJointComponent;
import Utils.PhysicsMotionState;

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
	auto staticSync = registry.view<StaticPhysicsComponent, SceneComponent>();
	staticSync.each([](StaticPhysicsComponent& physComp, SceneComponent& nodeComp)
		{
			auto pos = nodeComp.pNode->_getDerivedPosition();
			auto rot = nodeComp.pNode->_getDerivedOrientation();
			physComp.pBody->setWorldTransform(btTransform(btQuaternion(rot.x, rot.y, rot.z, rot.w), btVector3(pos.x, pos.y, pos.z)));
		});

	auto kinematicSync = registry.view<KinematicPhysicsComponent, SceneComponent>();
	kinematicSync.each([](KinematicPhysicsComponent& physComp, SceneComponent& nodeComp)
		{
			auto pos = nodeComp.pNode->_getDerivedPosition();
			auto rot = nodeComp.pNode->_getDerivedOrientation();
			physComp.pBody->setWorldTransform(btTransform(btQuaternion(rot.x, rot.y, rot.z, rot.w), btVector3(pos.x, pos.y, pos.z)));
		});

	m_pDynamicsWorld->stepSimulation(btScalar(deltaSec), 1, btScalar(fixedTimestep));

	/*
	auto dynamicSync = registry.view<DynamicPhysicsComponent, SceneComponent>();
	dynamicSync.each([](DynamicPhysicsComponent& physComp, SceneComponent& nodeComp)
		{
			auto& trans = physComp.pBody->getWorldTransform();
			auto& physPos = trans.getOrigin();
			auto& physRot = trans.getRotation();
			nodeComp.pNode->setPosition(*reinterpret_cast<Ogre::Vector3*>(&physPos));
			nodeComp.pNode->setOrientation(Ogre::Quaternion(physRot.w(), physRot.x(), physRot.y(), physRot.z()));
		});*/
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
	delete pShape;
}

btRigidBody* PhysicsSystem::createRigidBody(entt::registry& registry, entt::entity entity, btCollisionShape* pShape, float mass)
{
	btVector3 localInertia(0, 0, 0);
	if (pShape && mass >= 0.0f)
	{
		if (pShape->getShapeType() == EMPTY_SHAPE_PROXYTYPE)
			btSphereShape(0.1f).calculateLocalInertia(mass, localInertia);
		else
			pShape->calculateLocalInertia(mass, localInertia);
	}
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, new PhysicsMotionState(entity, registry), pShape, localInertia);
	return new btRigidBody(rbInfo);
}

DynamicPhysicsComponent& PhysicsSystem::addDynamicPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape, float mass)
{
	OGRE_ASSERT(registry.try_get<SceneComponent>(entity));
	OGRE_ASSERT(mass > 0.0f);
	SceneComponent& sceneComponent = registry.get<SceneComponent>(entity);
	OGRE_ASSERT(!sceneComponent.pNode->isStatic());

	DynamicPhysicsComponent& component = registry.emplace<DynamicPhysicsComponent>(entity);
	component.pBody = createRigidBody(registry, entity, pShape, mass);

	m_pDynamicsWorld->addRigidBody(component.pBody);
	OGRE_ASSERT(!component.pBody->isStaticOrKinematicObject());

	return component;
}
StaticPhysicsComponent& PhysicsSystem::addStaticPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape)
{
	OGRE_ASSERT(registry.try_get<SceneComponent>(entity));
	SceneComponent& sceneComponent = registry.get<SceneComponent>(entity);
	OGRE_ASSERT(sceneComponent.pNode->isStatic());

	StaticPhysicsComponent& component = registry.emplace<StaticPhysicsComponent>(entity);
	component.pBody = createRigidBody(registry, entity, pShape, 0.0f);

	m_pDynamicsWorld->addRigidBody(component.pBody);

	return component;
}

KinematicPhysicsComponent& PhysicsSystem::addKinematicPhysicsComponent(entt::registry& registry, entt::entity entity, btCollisionShape* pShape)
{
	OGRE_ASSERT(registry.try_get<SceneComponent>(entity));
	SceneComponent& sceneComponent = registry.get<SceneComponent>(entity);
	OGRE_ASSERT(!sceneComponent.pNode->isStatic());

	KinematicPhysicsComponent& component = registry.emplace<KinematicPhysicsComponent>(entity);
	component.pBody = createRigidBody(registry, entity, pShape, 0.0f);

	auto collisionFlags = component.pBody->getCollisionFlags();
	collisionFlags &= ~btCollisionObject::CF_STATIC_OBJECT;
	collisionFlags |= btCollisionObject::CF_KINEMATIC_OBJECT;
	component.pBody->setCollisionFlags(collisionFlags);
	component.pBody->setActivationState(DISABLE_DEACTIVATION);

	m_pDynamicsWorld->addRigidBody(component.pBody);

	return component;
}

void PhysicsSystem::destroyRigidBody(btRigidBody* pBody)
{
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
}

SpringJointComponent& PhysicsSystem::addSpringJointComponent(entt::registry& registry, entt::entity entity, btRigidBody* pBody1, btRigidBody* pBody2)
{
	SpringJointComponent& springComponent = registry.emplace<SpringJointComponent>(entity);
	btGeneric6DofSpringConstraint* pSpring = new btGeneric6DofSpringConstraint(*pBody1, *pBody2,
		btTransform(btQuaternion::getIdentity(), { 0, 0, 0 }),
		btTransform(btQuaternion::getIdentity(), { 0, 0, 0 }), false);
	springComponent.pSpring = pSpring;
	m_pDynamicsWorld->addConstraint(pSpring);
	return springComponent;
}