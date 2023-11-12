module;

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>
#include <OgreAssert.h>
#include <OgreLogManager.h>
#include <OgreStringConverter.h>

#include <SDL_scancode.h>

module Systems.BoatControlSystem;

import Systems.InputSystem;
import Systems.GraphicsSystem;
import Utils.DebugDrawer;
import Utils.MathUtils;

import Components.BoatControlComponent;
import Components.DynamicPhysicsComponent;

import World;

BoatControlSystem::BoatControlSystem(World& world, entt::registry& registry) : m_world(world), m_registry(registry) {}
BoatControlSystem::~BoatControlSystem() {}

void BoatControlSystem::initialize()
{
}

void BoatControlSystem::update(double deltaSec)
{
	auto boatControlUpdate = m_registry.view<BoatControlComponent, DynamicPhysicsComponent>();
	boatControlUpdate.each([&](BoatControlComponent& boatComp, DynamicPhysicsComponent& physComp)
		{
			btRigidBody* pBody = physComp.pBody;
			float rotationSpeed = 0.2f;

			if (boatComp.forwardBack != 0.0f)
			{
				pBody->applyCentralForce(pBody->getWorldTransform().getBasis().getColumn(2) * -boatComp.forwardBack * 2000.0f);
			}

			// Todo scale rotation speed based on forward velocity?
			if (boatComp.rightLeft != 0.0f)
			{
				pBody->applyTorque(pBody->getInvInertiaTensorWorld().inverse() * btVector3(0, -boatComp.rightLeft * rotationSpeed, 0));
			}

			/* // rotational movement along other axis for testing physics
			if (m_world.getInput().isKeyCurrentlyPressed(SDL_SCANCODE_E))
			{
				btVector3 torgue = (pBody->getWorldTransform().getBasis() * btVector3(0, 0, -1));
				pBody->applyTorque(pBody->getInvInertiaTensorWorld().inverse() * torgue * rotationSpeed);
			}
			if (m_world.getInput().isKeyCurrentlyPressed(SDL_SCANCODE_Q))
			{
				btVector3 torgue = (pBody->getWorldTransform().getBasis() * btVector3(0, 0, 1));
				pBody->applyTorque(pBody->getInvInertiaTensorWorld().inverse() * torgue * rotationSpeed);
			}
			if (m_world.getInput().isKeyCurrentlyPressed(SDL_SCANCODE_R))
			{
				btVector3 torgue =  (pBody->getWorldTransform().getBasis() * btVector3(1, 0, 0));
				pBody->applyTorque(pBody->getInvInertiaTensorWorld().inverse() * torgue * rotationSpeed);
			}
			if (m_world.getInput().isKeyCurrentlyPressed(SDL_SCANCODE_F))
			{
				btVector3 torgue =  (pBody->getWorldTransform().getBasis() * btVector3(-1, 0, 0));
				pBody->applyTorque(pBody->getInvInertiaTensorWorld().inverse() * torgue * rotationSpeed);
			}*/
		});
}

BoatControlComponent& BoatControlSystem::addBoatControlComponent(entt::entity entity)
{
	BoatControlComponent& boatControlComponent = m_registry.emplace<BoatControlComponent>(entity);
	return boatControlComponent;
}

void BoatControlSystem::removeBoatControlComponent(entt::entity entity)
{
	const auto& component = m_registry.get<BoatControlComponent>(entity);
	size_t numRemoved = m_registry.remove<BoatControlComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}