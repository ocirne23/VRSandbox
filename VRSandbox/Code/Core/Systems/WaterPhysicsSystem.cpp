module;

#include <entt/entity/registry.hpp>
#include <btBulletDynamicsCommon.h>
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
			float height = scene.pNode->getPosition().y;
			auto* pBody = phys.pBody;
			float baseForce = pow(fmin((targetHeight / height), 1.5f), 5.f);
			float force = baseForce * water.bouyancy;
			if (height > targetHeight)
			{
				force = 0.0f;
			}
			pBody->applyCentralForce(btVector3(0, force, 0));
			float x, y, z;
			pBody->getWorldTransform().getRotation().inverse().getEulerZYX(z, y, x);
			pBody->applyTorque(water.rightingTorque * btVector3(x, y, z));
		});

	//Ogre::LogManager::getSingleton().logMessage("baseForce: " + Ogre::StringConverter::toString(baseForce));
	//Ogre::LogManager::getSingleton().logMessage("target: " + Ogre::StringConverter::toString(targetHeight) + " height: " + Ogre::StringConverter::toString(height));
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
