module;

#include <entt/fwd.hpp>

export module Systems.BoatControlSystem;

export struct BoatControlComponent;

export class World;

export class BoatControlSystem
{
public:
	
	BoatControlSystem(World& world, entt::registry& registry);
	virtual ~BoatControlSystem();
	BoatControlSystem(const BoatControlSystem& copy) = delete;

	void initialize();
	void update(double deltaSec);

	BoatControlComponent& addBoatControlComponent(entt::entity entity);
	void removeBoatControlComponent(entt::entity entity);
	
private:

	World& m_world;
	entt::registry& m_registry;
};