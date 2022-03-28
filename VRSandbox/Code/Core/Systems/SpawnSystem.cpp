#include "SpawnSystem.h"

#include "Entity/TestEntities.h"

#include <OgreAssert.h>

SpawnSystem* SpawnSystem::s_instance = nullptr;

SpawnSystem::SpawnSystem(Ogre::SceneManager* pSceneManager, PhysicsSystem* pPhysics) :
	m_pSceneManager(pSceneManager),
	m_pPhysics(pPhysics)
{
	OGRE_ASSERT(s_instance == nullptr);
	s_instance = this;
}

SpawnSystem::~SpawnSystem() 
{
	s_instance = nullptr;
}