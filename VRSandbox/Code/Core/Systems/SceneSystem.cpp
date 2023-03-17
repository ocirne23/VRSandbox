module;

#include <OgreAssert.h>
#include <OgreRoot.h>
#include <entt/entity/registry.hpp>
#include <OgreResource.h>

module Systems.SceneSystem;

import Systems.GraphicsSystem;
import Components.SceneComponent;

SceneSystem::SceneSystem(GraphicsSystem* pGraphics) : 
	m_sceneManager(*pGraphics->getSceneManager())
{

}

SceneSystem::~SceneSystem()
{

}

SceneComponent& SceneSystem::addSceneComponentFromNode(entt::registry& registry, entt::entity entity, Ogre::SceneNode* pNode)
{
	SceneComponent& comp = registry.emplace<SceneComponent>(entity);
	comp.pNode = pNode;
	return comp;
}

SceneComponent& SceneSystem::addSceneNodeComponentWithParent(entt::registry& registry, entt::entity entity, Ogre::SceneNode* pParentNode, const Ogre::Vector3& position, const Ogre::Quaternion& orientation)
{
	SceneComponent& comp = registry.emplace<SceneComponent>(entity);
	comp.pNode = pParentNode->createChildSceneNode(pParentNode->isStatic() ? Ogre::SCENE_STATIC : Ogre::SCENE_DYNAMIC, position, orientation);
	return comp;
}

SceneComponent& SceneSystem::addSceneNodeComponent(entt::registry& registry, entt::entity entity, Ogre::SceneMemoryMgrTypes nodeType, const Ogre::Vector3& position, const Ogre::Quaternion& orientation)
{
	SceneComponent& comp = registry.emplace<SceneComponent>(entity);
	comp.pNode = m_sceneManager.getRootSceneNode(nodeType)->createChildSceneNode(nodeType, position, orientation);
	return comp;
}

void SceneSystem::removeSceneNodeComponent(entt::registry& registry, entt::entity entity)
{
	size_t numRemoved = registry.remove<SceneComponent>(entity);
	OGRE_ASSERT(numRemoved);
}

// idk
Ogre::Resource::Listener::~Listener() {}