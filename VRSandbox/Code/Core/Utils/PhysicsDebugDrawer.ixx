module;

#include <LinearMath/btIDebugDraw.h>

export module Utils.PhysicsDebugDrawer;

export class DebugDrawer;

export class PhysicsDebugDrawer : public btIDebugDraw
{
public:

	PhysicsDebugDrawer(DebugDrawer* pDebugDrawer);
	virtual ~PhysicsDebugDrawer();

	virtual void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override;
	virtual void drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color) override;
	virtual void reportErrorWarning(const char* warningString) override;
	virtual void draw3dText(const btVector3& location, const char* textString) override;
	
	virtual void setDebugMode(int debugMode)
	{
		m_debugMode = debugMode;
	}
	
	virtual int getDebugMode() const
	{
		return m_debugMode;
	}

private:

	DebugDrawer* m_pDebugDrawer = nullptr;
	int m_debugMode = 0;
};