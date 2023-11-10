module;

#include "OgrePrerequisites.h"

#include <memory.h>
#include <entt/entity/entity.hpp>

export module TestScene;

export class GraphicsSystem;
export class PhysicsSystem;
export class SceneSystem;
export class VRInputSystem;
export struct HandSkeletonData;

export class World;

export class TestScene
{
public:

	TestScene(World& world);
	virtual ~TestScene();
	TestScene(const TestScene& copy) = delete;

	void createScene();
	void update(double deltaSec);

private:

	World& m_world;

	Ogre::SceneNode* m_lightNodes[2] = {};
	Ogre::SceneNode* m_controllerNodes[2] = {};
	entt::entity m_handAnchorEntities[2] = { entt::null, entt::null };
	entt::entity m_handEntities[2] = { entt::null, entt::null };

	entt::entity m_boatEntity = entt::null;
	entt::entity m_waterEntity = entt::null;

};