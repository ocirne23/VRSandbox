#pragma once

#include <entt/fwd.hpp>

class GraphicsSystem;
class PhysicsSystem;
class SceneSystem;
class VRInputSystem;
struct HandSkeletonData;

namespace VRHandEntities
{
	entt::entity createLeftHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput);
	entt::entity createRightHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput);
}