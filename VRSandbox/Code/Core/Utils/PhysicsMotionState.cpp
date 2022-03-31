#include "PhysicsMotionState.h"

#include "Components/SceneComponent.h"

#include <OgreSceneNode.h>

#include <entt/entity/registry.hpp>

void PhysicsMotionState::getWorldTransform(btTransform& worldTrans) const
{
	const auto& sceneComp = registry.get<SceneComponent>(entity);
	const auto& pos = sceneComp.pNode->_getDerivedPositionUpdated();
	const auto& ori = sceneComp.pNode->_getDerivedOrientation();

	//TODO: evaluate reinterpret_cast
	worldTrans.setOrigin(btVector3(pos.x, pos.y, pos.z));
	worldTrans.setRotation(btQuaternion(ori.x, ori.y, ori.z, ori.w));
}

void PhysicsMotionState::setWorldTransform(const btTransform& worldTrans)
{
	auto& sceneComp = registry.get<SceneComponent>(entity);
	const auto& pos = worldTrans.getOrigin();
	const auto& ori = worldTrans.getRotation();

	if (sceneComp.pNode->isStatic())
		__debugbreak();

	//TODO: evaluate reinterpret_cast
	sceneComp.pNode->setPosition(Ogre::Vector3(pos.x(), pos.y(), pos.z()));
	sceneComp.pNode->setOrientation(Ogre::Quaternion(ori.w(), ori.x(), ori.y(), ori.z()));
}
