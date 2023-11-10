module;

#include <entt/fwd.hpp>
#include <OgreVector3.h>
#include <OgreQuaternion.h>
#include <OgreRoot.h>

export module Systems.SceneSystem;

export class World;
export class GraphicsSystem;
export struct SceneComponent;

export class SceneSystem
{
public:

	SceneSystem(World& world, entt::registry& registry);
	virtual ~SceneSystem();
	SceneSystem(const SceneSystem& copy) = delete;

	void initialize(GraphicsSystem* pGraphics);

	SceneComponent& addSceneComponentFromNode(entt::entity entity, Ogre::SceneNode* pNode);
	SceneComponent& addSceneNodeComponentWithParent(entt::entity entity, Ogre::SceneNode* pParentNode,
		const Ogre::Vector3& position = Ogre::Vector3::ZERO, const Ogre::Quaternion& orientation = Ogre::Quaternion::IDENTITY);
	SceneComponent& addSceneNodeComponent(entt::entity entity, Ogre::SceneMemoryMgrTypes nodeType, 
		const Ogre::Vector3& position = Ogre::Vector3::ZERO, const Ogre::Quaternion& orientation = Ogre::Quaternion::IDENTITY);
	void removeSceneNodeComponent(entt::entity entity);

	Ogre::SceneManager& getSceneManager() { return *m_pSceneManager; }

private:

	World& m_world;
	entt::registry& m_registry;

	Ogre::SceneManager* m_pSceneManager = nullptr;
};