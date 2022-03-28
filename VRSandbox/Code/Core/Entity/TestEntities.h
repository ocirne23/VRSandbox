#pragma once

#include <entt/fwd.hpp>
#include <OgreVector3.h>

class GraphicsSystem;
class PhysicsSystem;
class SceneSystem;

namespace TestEntities
{
	entt::entity createTestFloorEntity(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene);
	entt::entity createTestSphere(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, Ogre::Vector3 pos);
};