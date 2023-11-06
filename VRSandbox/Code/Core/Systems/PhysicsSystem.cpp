module;

#include <btBulletDynamicsCommon.h>
#include <OgreAssert.h>
#include <OgreVector3.h>
#include <OgreSceneNode.h>
#include <entt/entity/registry.hpp>

#include <OgreMeshManager2.h>
#include <OgreMesh2.h>
#include <OgreSubMesh2.h>
#include <Vao/OgreVertexArrayObject.h>
#include <Vao/OgreIndexBufferPacked.h>
#include <Vao/OgreAsyncTicket.h>
#include <OgreAsyncTextureTicket.h>
#include <OgreVertexIndexData.h>
#include <OgreRenderable.h>
#include <OgreBitwise.h>

module Systems.PhysicsSystem;

import Components.DynamicPhysicsComponent;
import Components.StaticPhysicsComponent;
import Components.KinematicPhysicsComponent;
import Components.SceneComponent;
import Components.SpringJointComponent;
import Components.FixedJointComponent;
import Utils.PhysicsMotionState;
import Utils.PhysicsDebugDrawer;

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

	m_pDynamicsWorld->debugDrawWorld();
}

void PhysicsSystem::setDebugDrawer(DebugDrawer* pDebugDrawer)
{
	m_pPhysicsDebugDrawer.reset(new PhysicsDebugDrawer(pDebugDrawer));
	m_pDynamicsWorld->setDebugDrawer(m_pPhysicsDebugDrawer.get());
}

void PhysicsSystem::setEnableDebugDraw(bool enable)
{
	if (enable)
		m_pPhysicsDebugDrawer->setDebugMode(btIDebugDraw::DBG_DrawWireframe | btIDebugDraw::DBG_DrawConstraints | btIDebugDraw::DBG_DrawContactPoints);
	else
		m_pPhysicsDebugDrawer->setDebugMode(btIDebugDraw::DBG_NoDebug);
}

btCollisionShape* PhysicsSystem::createBoxShape(const Ogre::Vector3& dimensions)
{
	//TODO: reuse shapes if dimensions are same?
	btCollisionShape* pShape = new btBoxShape(btVector3(btScalar(dimensions.x), btScalar(dimensions.y), btScalar(dimensions.z)));
	pShape->setUserIndex(1);
	m_shapes.emplace_back(pShape);
	return pShape;
}

btCollisionShape* PhysicsSystem::createSphereShape(Ogre::Real radius)
{
	btCollisionShape* pShape = new btSphereShape(btScalar(radius));
	pShape->setUserIndex(1);
	m_shapes.emplace_back(pShape);
	return pShape;
}

btCollisionShape* PhysicsSystem::createConeShape(Ogre::Real radius, Ogre::Real height)
{
	btCollisionShape* pShape = new btConeShape(btScalar(radius), btScalar(height * 2.0f));
	pShape->setUserIndex(1);
	m_shapes.emplace_back(pShape);
	return pShape;
}

btCollisionShape* PhysicsSystem::createCapsuleShape(Ogre::Real radius, Ogre::Real height)
{
	btCollisionShape* pShape = new btCapsuleShape(btScalar(radius), btScalar(height * 2.0f));
	pShape->setUserIndex(1);
	m_shapes.emplace_back(pShape);
	return pShape;
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

btCollisionShape* PhysicsSystem::createTriangleShapeFromOgreMesh(Ogre::String meshName)
{
	if (m_triangleShapeMap.find(meshName) != m_triangleShapeMap.end())
		return m_triangleShapeMap[meshName];

	Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().load(meshName, Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);

	const Ogre::Vector3 position = Ogre::Vector3(0, 0, 0);
	const Ogre::Quaternion& orient = Ogre::Quaternion::IDENTITY;
	const Ogre::Vector3 scale = Ogre::Vector3(1, 1, 1);

	size_t numVertices = 0;
	size_t numIndices = 0;

	Ogre::Mesh::SubMeshVec::const_iterator subMeshIterator = mesh->getSubMeshes().begin();

	while (subMeshIterator != mesh->getSubMeshes().end())
	{
		Ogre::SubMesh* subMesh = *subMeshIterator;
		numVertices += subMesh->mVao[0][0]->getVertexBuffers()[0]->getNumElements();
		numIndices += subMesh->mVao[0][0]->getIndexBuffer()->getNumElements();

		subMeshIterator++;
	}

	Ogre::Vector3* vertices = new Ogre::Vector3[numVertices];
	Ogre::uint32* indices = new Ogre::uint32[numIndices];

	size_t addedVertices = 0;
	size_t addedIndices = 0;

	size_t index_offset = 0;
	size_t subMeshOffset = 0;

	// Read Submeshes
	subMeshIterator = mesh->getSubMeshes().begin();
	while (subMeshIterator != mesh->getSubMeshes().end())
	{
		Ogre::SubMesh* subMesh = *subMeshIterator;
		Ogre::VertexArrayObjectArray vaos = subMesh->mVao[0];

		if (!vaos.empty())
		{
			//Get the first LOD level 
			Ogre::VertexArrayObject* vao = vaos[0];
			bool indices32 = (vao->getIndexBuffer()->getIndexType() == Ogre::IndexBufferPacked::IT_32BIT);

			const Ogre::VertexBufferPackedVec& vertexBuffers = vao->getVertexBuffers();
			Ogre::IndexBufferPacked* indexBuffer = vao->getIndexBuffer();

			//request async read from buffer 
			Ogre::VertexArrayObject::ReadRequestsVec requests;
			requests.push_back(Ogre::VertexArrayObject::ReadRequests(Ogre::VES_POSITION));

			vao->readRequests(requests);
			vao->mapAsyncTickets(requests);
			size_t subMeshVerticiesNum = requests[0].vertexBuffer->getNumElements();
			if (requests[0].type == Ogre::VET_HALF4)
			{
				for (size_t i = 0; i < subMeshVerticiesNum; ++i)
				{
					const Ogre::uint16* pos = reinterpret_cast<const Ogre::uint16*>(requests[0].data);
					Ogre::Vector3 vec;
					vec.x = Ogre::Bitwise::halfToFloat(pos[0]);
					vec.y = Ogre::Bitwise::halfToFloat(pos[1]);
					vec.z = Ogre::Bitwise::halfToFloat(pos[2]);
					requests[0].data += requests[0].vertexBuffer->getBytesPerElement();
					vertices[i + subMeshOffset] = (orient * (vec * scale)) + position;
				}
			}
			else if (requests[0].type == Ogre::VET_FLOAT3)
			{
				for (size_t i = 0; i < subMeshVerticiesNum; ++i)
				{
					const float* pos = reinterpret_cast<const float*>(requests[0].data);
					Ogre::Vector3 vec;
					vec.x = *pos++;
					vec.y = *pos++;
					vec.z = *pos++;
					requests[0].data += requests[0].vertexBuffer->getBytesPerElement();
					vertices[i + subMeshOffset] = (orient * (vec * scale)) + position;
				}
			}
			else
			{
				__debugbreak();
			}
			subMeshOffset += subMeshVerticiesNum;
			vao->unmapAsyncTickets(requests);

			////Read index data
			if (indexBuffer)
			{
				Ogre::AsyncTicketPtr asyncTicket = indexBuffer->readRequest(0, indexBuffer->getNumElements());

				unsigned int* pIndices = 0;
				if (indices32)
				{
					pIndices = (unsigned*)(asyncTicket->map());
				}
				else
				{
					unsigned short* pShortIndices = (unsigned short*)(asyncTicket->map());
					pIndices = new unsigned int[indexBuffer->getNumElements()];
					for (size_t k = 0; k < indexBuffer->getNumElements(); k++) pIndices[k] = static_cast<unsigned int>(pShortIndices[k]);
				}
				unsigned int bufferIndex = 0;

				for (size_t i = addedIndices; i < addedIndices + indexBuffer->getNumElements(); i++)
				{
					indices[i] = pIndices[bufferIndex] + (Ogre::uint32)index_offset;
					bufferIndex++;
				}
				addedIndices += indexBuffer->getNumElements();

				if (!indices32) delete[] pIndices;

				asyncTicket->unmap();
			}
			index_offset += vertexBuffers[0]->getNumElements();
		}
		subMeshIterator++;
	}

	// The Bullet triangle mesh
	btTriangleMesh* triMesh = new btTriangleMesh();
	btVector3 vert0, vert1, vert2;
	int i = 0;

	for (unsigned int y = 0; y < numIndices / 3; y++)
	{
		vert0.setValue(vertices[indices[i]].x, vertices[indices[i]].y, vertices[indices[i]].z);
		vert1.setValue(vertices[indices[i + 1]].x, vertices[indices[i + 1]].y, vertices[indices[i + 1]].z);
		vert2.setValue(vertices[indices[i + 2]].x, vertices[indices[i + 2]].y, vertices[indices[i + 2]].z);
		triMesh->addTriangle(vert0, vert1, vert2);
		i += 3;
	}

	// --------------- Cleanup

	delete[] vertices;
	delete[] indices;

	// Create the collision shape
	btConvexTriangleMeshShape* pShape = new btConvexTriangleMeshShape(triMesh);
	pShape->setUserIndex(1);
	m_shapes.push_back(pShape);
	m_triangleShapeMap.insert(std::make_pair(meshName, pShape));
	return pShape;
}