module;

#include <memory>
#include <entt/fwd.hpp>

export module Systems.SpawnSystem;

export class PhysicsSystem;
export namespace Ogre { class SceneManager; }
export struct DynamicPhysicsComponent;

export class SpawnSystem
{
public:
	
	SpawnSystem(Ogre::SceneManager* pSceneManager, PhysicsSystem* pPhysics);
	virtual ~SpawnSystem();
	SpawnSystem(const SpawnSystem& copy) = delete;

	//static SpawnSystem& getInstance();

	PhysicsSystem& getPhysicsSystem() { return *m_pPhysics; }
	Ogre::SceneManager& getSceneManager() { return *m_pSceneManager; }

private:

	static SpawnSystem* s_instance;

	Ogre::SceneManager* m_pSceneManager = nullptr;
	PhysicsSystem* m_pPhysics = nullptr;
};