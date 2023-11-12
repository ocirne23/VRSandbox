module;

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>
#include <OgreLogManager.h>
#include <OgreStringConverter.h>
#include <OgreSceneNode.h>

module Systems.WaterPhysicsSystem;

import Components.WaterPhysicsComponent;
import Components.DynamicPhysicsComponent;
import Components.SceneComponent;
import Entity.TestEntities;
import World;

WaterPhysicsSystem::WaterPhysicsSystem(World& world, entt::registry& registry) : m_world(world), m_registry(registry) {}
WaterPhysicsSystem::~WaterPhysicsSystem() {}

void WaterPhysicsSystem::initialize()
{
	m_waterEntity = TestEntities::createTestWater(m_world, Ogre::Vector3(0, 0, 0));
}

void WaterPhysicsSystem::update(double deltaSec)
{
	m_timePassed += deltaSec;
	const float targetHeight = 15.0f + sinf((float)m_timePassed * m_waveFrequency) * 5.0f;
	m_registry.get<SceneComponent>(m_waterEntity).pNode->setPosition(5, targetHeight, 0);

	auto bouyuancyUpdate = m_registry.view<SceneComponent, WaterPhysicsComponent, DynamicPhysicsComponent>();
	bouyuancyUpdate.each([&](SceneComponent& scene, WaterPhysicsComponent& water, DynamicPhysicsComponent& phys)
		{
			auto* pBody = phys.pBody;
			
			btVector3 aabbMin, aabbMax;
			pBody->getAabb(aabbMin, aabbMax);

			const float maxHeight = aabbMax.y();
			const float minHeight = aabbMin.y();
			const float heightRange = maxHeight - minHeight;
			const float bouyuancyPercentage = Ogre::Math::Clamp((targetHeight - minHeight) / heightRange, 0.0f, 1.0f);
			const float bouyancyForce = water.bouyancyForce * bouyuancyPercentage;

			pBody->applyCentralImpulse(pBody->getInvInertiaTensorWorld().inverse() * btVector3(0, bouyancyForce * (float)deltaSec, 0));
			if (bouyuancyPercentage > 0.0f) // only apply torgue if in water
			{
				const float rightingForce = 2.0f;
				const btVector3 up(0, 1, 0); // todo, wave angle
				btVector3 bodyUp = pBody->getWorldTransform().getBasis() * up;
				btVector3 direction = bodyUp.cross(up);
				pBody->applyTorqueImpulse(pBody->getInvInertiaTensorWorld().inverse() * direction * rightingForce * (float)deltaSec);
			}
		});
}

WaterPhysicsComponent& WaterPhysicsSystem::addWaterPhysicsComponent(entt::entity entity)
{
	WaterPhysicsComponent& waterPhysicsComponent = m_registry.emplace<WaterPhysicsComponent>(entity);
	return waterPhysicsComponent;
}

void WaterPhysicsSystem::removeWaterPhysicsComponent(entt::entity entity)
{
	const auto& component = m_registry.get<WaterPhysicsComponent>(entity);
	size_t numRemoved = m_registry.remove<WaterPhysicsComponent>(entity);
	OGRE_ASSERT(numRemoved == 1);
}
