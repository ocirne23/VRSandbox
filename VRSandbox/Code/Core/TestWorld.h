#pragma once

#include "OgrePrerequisites.h"

#include <memory.h>
#include <entt/entity/entity.hpp>

class GraphicsSystem;
class PhysicsSystem;
class SceneSystem;
struct HandSkeletonData;

class TestWorld
{
public:

	TestWorld(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene);
	virtual ~TestWorld();
	TestWorld(const TestWorld& copy) = delete;

	void createScene();

	void setVRHandTrackingData(const HandSkeletonData& left, const HandSkeletonData& right);

private:

	entt::registry& m_registry;
	GraphicsSystem& m_graphics;
	PhysicsSystem& m_physics; 
	SceneSystem& m_scene;

	Ogre::SceneNode* m_lightNodes[3] = {};
	Ogre::SceneNode* m_controllerNodes[2] = {};
	entt::entity m_handAnchorEntities[2] = { entt::null, entt::null };
	entt::entity m_handEntities[2] = { entt::null, entt::null };
};