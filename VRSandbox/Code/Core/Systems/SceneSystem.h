#pragma once

#include <entt/fwd.hpp>

class GraphicsSystem;
struct SceneComponent;

namespace Ogre { class SceneManager; enum SceneMemoryMgrTypes; }

class SceneSystem
{
public:

	SceneSystem(GraphicsSystem* pGraphics);
	virtual ~SceneSystem();
	SceneSystem(const SceneSystem& copy) = delete;

	SceneComponent& addSceneNodeComponent(entt::registry& registry, entt::entity entity, Ogre::SceneMemoryMgrTypes nodeType);
	void removeSceneNodeComponent(entt::registry& registry, entt::entity entity);

	Ogre::SceneManager& getSceneManager() { return m_sceneManager; }

private:

	Ogre::SceneManager& m_sceneManager;
};