#pragma once

#include <entt/fwd.hpp>
#include <OgreVector3.h>

class GraphicsSystem;
class PhysicsSystem;
class SceneSystem;

struct HandSkeletonData;

namespace VRHandEntities
{
	entt::entity createLeftHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const HandSkeletonData& handData);

	entt::entity createRightHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, const HandSkeletonData& handData);

	//entt::entity createHandAnchor(entt::registry& registry, Graphics* pGraphics, Physics* pPhysics, entt::entity handEntity);
}