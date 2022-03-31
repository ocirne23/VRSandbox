#pragma once

#include <entt/fwd.hpp>

#include <OgreVector3.h>
#include <OgreQuaternion.h>

class GraphicsSystem;
struct SceneComponent;

namespace Ogre { class SceneManager; enum SceneMemoryMgrTypes; class SceneNode; }

class SceneSystem
{
public:

	SceneSystem(GraphicsSystem* pGraphics);
	virtual ~SceneSystem();
	SceneSystem(const SceneSystem& copy) = delete;

	SceneComponent& addSceneComponentFromNode(entt::registry& registry, entt::entity entity, Ogre::SceneNode* pNode);
	SceneComponent& addSceneNodeComponentWithParent(entt::registry& registry, entt::entity entity, Ogre::SceneNode* pParentNode,
		const Ogre::Vector3& position = Ogre::Vector3::ZERO, const Ogre::Quaternion& orientation = Ogre::Quaternion::IDENTITY);
	SceneComponent& addSceneNodeComponent(entt::registry& registry, entt::entity entity, Ogre::SceneMemoryMgrTypes nodeType, 
		const Ogre::Vector3& position = Ogre::Vector3::ZERO, const Ogre::Quaternion& orientation = Ogre::Quaternion::IDENTITY);
	void removeSceneNodeComponent(entt::registry& registry, entt::entity entity);

	Ogre::SceneManager& getSceneManager() { return m_sceneManager; }

private:

	Ogre::SceneManager& m_sceneManager;
};