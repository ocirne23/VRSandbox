module;

#include <entt/fwd.hpp>

export module Entity.VRHandEntities;

export class World;
export enum class EHandType : char;

export namespace VRHandEntities
{
	entt::entity createHand(World& world, EHandType hand);
}