module;

#include <OgreRoot.h>

module Utils.PhysicsDebugDrawer;

import Utils.DebugDrawer;

PhysicsDebugDrawer::PhysicsDebugDrawer(DebugDrawer* pDebugDrawer) : m_pDebugDrawer(pDebugDrawer)
{
	
}

PhysicsDebugDrawer::~PhysicsDebugDrawer()
{

}

void PhysicsDebugDrawer::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
	m_pDebugDrawer->drawLine(Ogre::Vector3(from.x(), from.y(), from.z()), Ogre::Vector3(to.x(), to.y(), to.z()), Ogre::ColourValue(color.x(), color.y(), color.z(), 1.0f));
}

void PhysicsDebugDrawer::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
{
	m_pDebugDrawer->drawCircle(Ogre::Vector3(PointOnB.x(), PointOnB.y(), PointOnB.z()), 0.05f, 5, Ogre::ColourValue(color.x(), color.y(), color.z(), 1.0f));
}

void PhysicsDebugDrawer::reportErrorWarning(const char* warningString)
{
}

void PhysicsDebugDrawer::draw3dText(const btVector3& location, const char* textString)
{
}
