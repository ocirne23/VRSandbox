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
import Components.FixedJointComponent;
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
	//OGRE_ASSERT(m_pDynamicsWorld->getNumCollisionObjects() == 0);
	
	for (auto* pShape : m_shapes)
		delete pShape;
	m_shapes.clear();

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

btCollisionShape* PhysicsSystem::createBoxShape(const Ogre::Vector3& dimensions)
{
	//TODO: reuse shapes if dimensions are same?
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

void PhysicsSystem::destroyRigidBody(btRigidBody* pBody)
{
	OGRE_ASSERT(pBody);

	m_pDynamicsWorld->removeRigidBody(pBody);

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
	delete pBody;
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
	component.pBody->setCollisionFlags(component.pBody->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
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

SpringJointComponent& PhysicsSystem::addSpringJointComponent(entt::registry& registry, entt::entity entity, 
	btRigidBody* pBody1, btRigidBody* pBody2, float stiffness, float damping,
	const Ogre::Vector3& attach1, const Ogre::Vector3& attach2, 
	const Ogre::Vector3& limitMin, const Ogre::Vector3& limitMax)
{
	SpringJointComponent& springComponent = registry.emplace<SpringJointComponent>(entity);
	btGeneric6DofSpring2Constraint* pSpring = new btGeneric6DofSpring2Constraint(*pBody1, *pBody2,
		btTransform(btQuaternion::getIdentity(), { attach1.x, attach1.y, attach1.z }),
		btTransform(btQuaternion::getIdentity(), { attach2.x, attach2.y, attach2.z }));
	springComponent.pSpring = pSpring;

	pSpring->setLinearLowerLimit(btVector3(limitMin.x, limitMin.y, limitMin.z));
	pSpring->setLinearUpperLimit(btVector3(limitMax.x, limitMax.y, limitMax.z));

	for (int i = 0; i < 3; ++i)
	{
		pSpring->enableSpring(i, true);
		pSpring->setStiffness(i, stiffness);
		pSpring->setDamping(i, damping);
		pSpring->setEquilibriumPoint(i, 0.0f);
	}

	m_pDynamicsWorld->addConstraint(pSpring);
	return springComponent;
}

FixedJointComponent& PhysicsSystem::addFixedJointComponent(entt::registry& registry, entt::entity entity, btRigidBody* pBody1, btRigidBody* pBody2, const Ogre::Vector3& attach1, const Ogre::Vector3& attach2)
{
	FixedJointComponent& fixedJointComponent = registry.emplace<FixedJointComponent>(entity);
	btFixedConstraint* pJoint = new btFixedConstraint(*pBody1, *pBody2,
		btTransform(btQuaternion::getIdentity(), { attach1.x, attach1.y, attach1.z }),
		btTransform(btQuaternion::getIdentity(), { attach2.x, attach2.y, attach2.z }));
	fixedJointComponent.pJoint = pJoint;
	m_pDynamicsWorld->addConstraint(pJoint);
	return fixedJointComponent;
}

void PhysicsSystem::removeDynamicPhysicsComponent(entt::registry& registry, entt::entity entity)
{
	const auto& component = registry.get<DynamicPhysicsComponent>(entity);
	destroyRigidBody(component.pBody);
	size_t numRemoved = registry.remove<DynamicPhysicsComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}

void PhysicsSystem::removeStaticPhysicsComponent(entt::registry& registry, entt::entity entity)
{
	const auto& component = registry.get<StaticPhysicsComponent>(entity);
	destroyRigidBody(component.pBody);
	size_t numRemoved = registry.remove<StaticPhysicsComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}

void PhysicsSystem::removeKinematicPhysicsComponent(entt::registry& registry, entt::entity entity)
{
	const auto& component = registry.get<KinematicPhysicsComponent>(entity);
	destroyRigidBody(component.pBody);
	size_t numRemoved = registry.remove<KinematicPhysicsComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}

void PhysicsSystem::removeSpringJointComponent(entt::registry& registry, entt::entity entity)
{
	const auto& component = registry.get<SpringJointComponent>(entity);
	m_pDynamicsWorld->removeConstraint(component.pSpring);
	delete component.pSpring;
	size_t numRemoved = registry.remove<SpringJointComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}

void PhysicsSystem::removeFixedJointComponent(entt::registry& registry, entt::entity entity)
{
	const auto& component = registry.get<FixedJointComponent>(entity);
	m_pDynamicsWorld->removeConstraint(component.pJoint);
	delete component.pJoint;
	size_t numRemoved = registry.remove<FixedJointComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}