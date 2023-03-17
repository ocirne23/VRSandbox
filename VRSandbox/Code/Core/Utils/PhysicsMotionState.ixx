module;

#include <LinearMath/btMotionState.h>
#include <entt/fwd.hpp>

export module Utils.PhysicsMotionState;

export struct PhysicsMotionState : public btMotionState
{
	PhysicsMotionState(entt::entity entity, entt::registry& registry) : entity(entity), registry(registry) {}
	~PhysicsMotionState() {}
	entt::entity entity;
	entt::registry& registry;

	virtual void getWorldTransform(btTransform& worldTrans) const override;
	virtual void setWorldTransform(const btTransform& worldTrans) override;
};