module;

export module Components.StaticPhysicsComponent;

export class btRigidBody;

export struct StaticPhysicsComponent
{
	btRigidBody* pBody = nullptr;
	int m_lastUpdateRevision = 0;
};