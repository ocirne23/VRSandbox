#pragma once

#include "OgrePrerequisites.h"

#include <memory.h>
#include <entt/entity/entity.hpp>

class GraphicsSystem;
class PhysicsSystem;
class SceneSystem;
class VRInputSystem;
struct HandSkeletonData;

class TestWorld
{
public:

	TestWorld(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput);
	virtual ~TestWorld();
	TestWorld(const TestWorld& copy) = delete;

	void createScene();

private:

	entt::registry& m_registry;
	GraphicsSystem& m_graphics;
	PhysicsSystem& m_physics; 
	SceneSystem& m_scene;
	VRInputSystem& m_vrInput;

	Ogre::SceneNode* m_lightNodes[3] = {};
	Ogre::SceneNode* m_controllerNodes[2] = {};
	entt::entity m_handAnchorEntities[2] = { entt::null, entt::null };
	entt::entity m_handEntities[2] = { entt::null, entt::null };
};