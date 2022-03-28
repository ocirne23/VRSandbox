#include "SceneSystem.h"

#include "GraphicsSystem.h"
#include "Components/SceneComponent.h"

#include <OgreAssert.h>
#include <OgreRoot.h>

#include <entt/entity/registry.hpp>

SceneSystem::SceneSystem(GraphicsSystem* pGraphics) : 
	m_sceneManager(*pGraphics->getSceneManager())
{

}

SceneSystem::~SceneSystem()
{

}

SceneComponent& SceneSystem::addSceneNodeComponent(entt::registry& registry, entt::entity entity, Ogre::SceneMemoryMgrTypes nodeType)
{
	SceneComponent& comp = registry.emplace<SceneComponent>(entity);
	comp.pNode = m_sceneManager.getRootSceneNode(nodeType)->createChildSceneNode(nodeType);
	OGRE_ASSERT(comp.pNode);
	return comp;
}

void SceneSystem::removeSceneNodeComponent(entt::registry& registry, entt::entity entity)
{
	size_t numRemoved = registry.remove<SceneComponent>(entity);
	OGRE_ASSERT(numRemoved);
}
