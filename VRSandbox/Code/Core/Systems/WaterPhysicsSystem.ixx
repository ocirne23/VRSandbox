module;

#include <entt/fwd.hpp>

export module Systems.WaterPhysicsSystem;
export struct WaterPhysicsComponent;
export class World;

export class WaterPhysicsSystem
{
public:

	WaterPhysicsSystem(World& world, entt::registry& registry);
	virtual ~WaterPhysicsSystem();
	WaterPhysicsSystem(const WaterPhysicsSystem& copy) = delete;

	void initialize();
	void update(double deltaSec);

	WaterPhysicsComponent& addWaterPhysicsComponent(entt::entity entity);
	void removeWaterPhysicsComponent(entt::entity entity);

private:

	World& m_world;
	entt::registry& m_registry;

	float m_waveFrequency = 0.5f;
	float m_waterHeight = 0.0f;
	double m_timePassed = 0.0f;

	entt::entity m_waterEntity;
};
