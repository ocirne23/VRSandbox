#pragma once

#include <memory>
#include <entt/fwd.hpp>

class PhysicsSystem;
namespace Ogre { class SceneManager; }

struct DynamicPhysicsComponent;

class SpawnSystem
{
public:
	
	SpawnSystem(Ogre::SceneManager* pSceneManager, PhysicsSystem* pPhysics);
	virtual ~SpawnSystem();
	SpawnSystem(const SpawnSystem& copy) = delete;

	static SpawnSystem& getInstance() { }

	PhysicsSystem& getPhysicsSystem() { return *m_pPhysics; }
	Ogre::SceneManager& getSceneManager() { return *m_pSceneManager; }

private:

	static SpawnSystem* s_instance;

	Ogre::SceneManager* m_pSceneManager = nullptr;
	PhysicsSystem* m_pPhysics = nullptr;
};