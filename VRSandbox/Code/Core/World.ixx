module;

#include <entt/fwd.hpp>

export module World;

import <memory>;

import Systems.GraphicsSystem;
import Systems.PhysicsSystem;
import Systems.SceneSystem;
import Systems.VRInputSystem;
import Systems.InputSystem;
import Systems.WaterPhysicsSystem;

export class TestScene;
export class CameraController;

/***
* The World class owns all systems and mirrors as a global object container for them.
* Use of this class should be kept to a minimum, instead required dependencies should be passed to systems as needed.
* Ideally this class should be created on the stack so system access can be optimized by the compiler.
*/
export class World
{
public:

	World(entt::registry& registry);
	virtual ~World();
	World(const World& copy) = delete;

	void initialize();
	void updateLoop();
	void shutdown() { m_wantsShutdown = true; }

	entt::registry& getRegistry() { return m_registry; }
	GraphicsSystem& getGraphics() { return m_graphics; }
	PhysicsSystem& getPhysics() { return m_physics; }
	SceneSystem& getScene() { return m_scene; }
	VRInputSystem& getVRInput() { return m_vrInput; }
	InputSystem& getInput() { return m_input; }
	WaterPhysicsSystem& getWaterPhysics() { return m_waterPhysics; }

private:

	void setupCamera();

private:

	entt::registry& m_registry;
	GraphicsSystem m_graphics;
	PhysicsSystem m_physics;
	SceneSystem m_scene;
	VRInputSystem m_vrInput;
	InputSystem m_input;
	WaterPhysicsSystem m_waterPhysics;

private:

	bool m_wantsShutdown = false;
	std::unique_ptr<TestScene> m_pTestScene;
	std::unique_ptr<CameraController> m_pCameraController;
};