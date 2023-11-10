module;

#include <OgreAssert.h>
#include <OgreRoot.h>
#include <entt/entity/registry.hpp>
#include <OgreResource.h>

module Systems.SceneSystem;

import Systems.GraphicsSystem;
import Components.SceneComponent;

SceneSystem::SceneSystem(World& world, entt::registry& registry) : m_world(world), m_registry(registry)
{

}

SceneSystem::~SceneSystem()
{

}

void SceneSystem::initialize(GraphicsSystem* pGraphics)
{
	m_pSceneManager = pGraphics->getSceneManager();
}

SceneComponent& SceneSystem::addSceneComponentFromNode(entt::entity entity, Ogre::SceneNode* pNode)
{
	SceneComponent& comp = m_registry.emplace<SceneComponent>(entity);
	comp.pNode = pNode;
	return comp;
}

SceneComponent& SceneSystem::addSceneNodeComponentWithParent(entt::entity entity, Ogre::SceneNode* pParentNode, const Ogre::Vector3& position, const Ogre::Quaternion& orientation)
{
	SceneComponent& comp = m_registry.emplace<SceneComponent>(entity);
	comp.pNode = pParentNode->createChildSceneNode(pParentNode->isStatic() ? Ogre::SCENE_STATIC : Ogre::SCENE_DYNAMIC, position, orientation);
	return comp;
}

SceneComponent& SceneSystem::addSceneNodeComponent(entt::entity entity, Ogre::SceneMemoryMgrTypes nodeType, const Ogre::Vector3& position, const Ogre::Quaternion& orientation)
{
	SceneComponent& comp = m_registry.emplace<SceneComponent>(entity);
	comp.pNode = m_pSceneManager->getRootSceneNode(nodeType)->createChildSceneNode(nodeType, position, orientation);
	return comp;
}

void SceneSystem::removeSceneNodeComponent(entt::entity entity)
{
	size_t numRemoved = m_registry.remove<SceneComponent>(entity);
	OGRE_ASSERT(numRemoved);
}

// idk
//Ogre::Resource::Listener::~Listener() {}