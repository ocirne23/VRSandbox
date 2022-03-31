#pragma once

#include <entt/fwd.hpp>

class GraphicsSystem;
class PhysicsSystem;
class SceneSystem;
class VRInputSystem;
struct HandSkeletonData;
enum class EHandType : char;

namespace VRHandEntities
{
	entt::entity createHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput, EHandType hand);
}