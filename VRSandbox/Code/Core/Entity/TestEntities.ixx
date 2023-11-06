module;

#include <entt/fwd.hpp>
#include <OgreVector3.h>

export module Entity.TestEntities;

export class GraphicsSystem;
export class PhysicsSystem;
export class SceneSystem;

export namespace TestEntities
{
	entt::entity createTestFloorEntity(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene);
	entt::entity createTestSphere(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos);
	entt::entity createTestCube(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const Ogre::Vector3& pos);
};