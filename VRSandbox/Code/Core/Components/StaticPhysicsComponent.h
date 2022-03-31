#pragma once

class btRigidBody;

struct StaticPhysicsComponent
{
	btRigidBody* pBody = nullptr;
	int m_lastUpdateRevision = 0;
};