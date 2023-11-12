module;

#include "OgrePrerequisites.h"

#include <memory.h>
#include <entt/entity/entity.hpp>

export module TestScene;

export class GraphicsSystem;
export class PhysicsSystem;
export class SceneSystem;
export class VRInputSystem;
export class CameraFlyingController;
export struct HandSkeletonData;

export class World;

export class TestScene
{
public:

	TestScene(World& world, entt::registry& registry);
	virtual ~TestScene();
	TestScene(const TestScene& copy) = delete;

	void createScene();
	void update(double deltaSec);

	void setupControls();

private:

	World& m_world;
	entt::registry& m_registry;

	std::unique_ptr<CameraFlyingController> m_pCameraFlyingController;
	Ogre::SceneNode* m_lightNodes[2] = {};
	Ogre::SceneNode* m_controllerNodes[2] = {};
	entt::entity m_handAnchorEntities[2] = { entt::null, entt::null };
	entt::entity m_handEntities[2] = { entt::null, entt::null };

	entt::entity m_boatEntity = entt::null;
	entt::entity m_waterEntity = entt::null;

};