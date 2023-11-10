module;

#include <entt/fwd.hpp>
#include <OgreVector3.h>

export module Entity.TestEntities;

export class World;

export namespace TestEntities
{
	entt::entity createTestFloorEntity(World& world);
	entt::entity createTestSphere(World& world, const Ogre::Vector3& pos);
	entt::entity createTestCube(World& world, const Ogre::Vector3& pos);
	entt::entity createTestCone(World& world, const Ogre::Vector3& pos);
	entt::entity createTestCapsule(World& world, const Ogre::Vector3& pos);
	entt::entity createTestRamp(World& world, const Ogre::Vector3& pos);
	entt::entity createTestWedge(World& world, const Ogre::Vector3& pos);
	entt::entity createTestBoat(World& world, const Ogre::Vector3& pos);
	entt::entity createTestWater(World& world, const Ogre::Vector3& pos);


};