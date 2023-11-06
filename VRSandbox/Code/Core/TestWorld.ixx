module;

#include "OgrePrerequisites.h"

#include <memory.h>
#include <entt/entity/entity.hpp>

export module TestWorld;

export class GraphicsSystem;
export class PhysicsSystem;
export class SceneSystem;
export class VRInputSystem;
export struct HandSkeletonData;

export class TestWorld
{
public:

	TestWorld(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput);
	virtual ~TestWorld();
	TestWorld(const TestWorld& copy) = delete;

	void createScene();

	void update(double deltaSec);

private:

	entt::registry& m_registry;
	GraphicsSystem& m_graphics;
	PhysicsSystem& m_physics; 
	SceneSystem& m_scene;
	VRInputSystem& m_vrInput;

	Ogre::SceneNode* m_lightNodes[2] = {};
	Ogre::SceneNode* m_controllerNodes[2] = {};
	entt::entity m_handAnchorEntities[2] = { entt::null, entt::null };
	entt::entity m_handEntities[2] = { entt::null, entt::null };
};