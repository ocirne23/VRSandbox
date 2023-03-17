module;

#include <entt/fwd.hpp>

export module Entity.VRHandEntities;

export class GraphicsSystem;
export class PhysicsSystem;
export class SceneSystem;
export class VRInputSystem;
export struct HandSkeletonData;
export enum class EHandType : char;

export namespace VRHandEntities
{
	entt::entity createHand(entt::registry& registry, GraphicsSystem& graphics, PhysicsSystem& physics, SceneSystem& scene, VRInputSystem& vrInput, EHandType hand);
}