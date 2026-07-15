export module App.InputControls;

import Core;
import Core.Allocator;
import Core.Log;
import Core.Window;
import Core.SDL;
import Core.Frustum;
import Core.Time;
import Core.glm;
import Core.Camera;

import Animation;
import File;
import Input;
import UI;
import RendererVK;
import Entity;
import Script;
import Physics;
import Audio;

export class InputControls
{
private:

    GizmoController& gizmo;
    FreeFlyCameraController& cameraController;
    World& world; // spawns go through the world's API; it owns the root entity lifetimes
    std::vector<RendererVKLayout::LightInfo>& spawnedLights;
    std::vector<EntityPtr>& spawnedLightGeom;
    std::vector<PhysicsJoint>& spawnedJoints;
    KeyboardListener* pKeyboardListener;

public:

    InputControls(
        GizmoController& gizmo,
        FreeFlyCameraController& cameraController,
        World& world,
        std::vector<RendererVKLayout::LightInfo>& spawnedLights,
        std::vector<EntityPtr>& spawnedLightGeom,
		std::vector<PhysicsJoint>& spawnedJoints)
        : gizmo(gizmo), cameraController(cameraController), world(world), spawnedLights(spawnedLights), spawnedLightGeom(spawnedLightGeom), spawnedJoints(spawnedJoints)
    {

        auto& input = Globals::input;
        pKeyboardListener = input.addKeyboardListener();
        pKeyboardListener->onKeyPressed = [this](const SDL_KeyboardEvent& evt) { handleKeyEvent(evt); };
        pKeyboardListener->onKeyReleased = [this](const SDL_KeyboardEvent& evt) { handleKeyEvent(evt); };
    }

	~InputControls()
	{
		auto& input = Globals::input;
		input.removeKeyboardListener(pKeyboardListener);
	}

    void handleKeyEvent(const SDL_KeyboardEvent& evt)
    {
        ScriptEventManager& scriptEvents = Globals::scriptEvents;
        Renderer& renderer = Globals::rendererVK;
        UI& ui = Globals::ui;

        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_W) scriptEvents.fireEvent(evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN ? "W Down" : "W Up");
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_A) scriptEvents.fireEvent(evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN ? "A Down" : "A Up");
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_S) scriptEvents.fireEvent(evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN ? "S Down" : "S Up");
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_D) scriptEvents.fireEvent(evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN ? "D Down" : "D Up");
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_LSHIFT) scriptEvents.fireEvent(evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN ? "LShift Down" : "LShift Up");
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_SPACE)  scriptEvents.fireEvent(evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN ? "Space Down" : "Space Up");

        if (evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_T) gizmo.setMode(EGizmoMode::Translate);
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_R) gizmo.setMode(EGizmoMode::Rotate);
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_G) gizmo.setMode(EGizmoMode::Scale);
        }
        if (evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN && !evt.repeat && (evt.mod & SDL_KMOD_CTRL) != 0)
        {
            // Node copy/paste for the Script editor, mirrored here so it also works as a plain global
            // shortcut (UI::copy/pasteScriptSelection no-op while an ImGui text field elsewhere has focus).
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_C) ui.copyScriptSelection();
            if (evt.scancode == SDL_Scancode::SDL_SCANCODE_V) ui.pasteScriptSelection();
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_F5 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            renderer.reloadShaders();
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_F6 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
			Globals::scriptHost.reloadCurrentScript(); // F6: recompile + hot-reload the script the Script panel is editing
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_P && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            renderer.toggleGiProbeDebug();          // P: show/hide GI probe debug cubes
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_O && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
            renderer.cycleGiProbeDebugMode();        // O: cycle irradiance <-> cellSize/LOD color
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_L && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            renderer.setSunLight(-cameraController.getDirection(), glm::vec3(1.0f), 5.0f); // aim the sun along the camera forward
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_1 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            spawnedLights.resize(0);
            spawnedLightGeom.resize(0);
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_2 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            spawnedLights.push_back(PointLight{ cameraController.getPosition(), 25.0f, glm::abs(glm::sphericalRand(1.0f)), 50.0f });
            spawnedLightGeom.push_back(world.spawn("sphere", Transform(cameraController.getPosition(), 0.1f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_3 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            spawnedLights.push_back(PointLight{ cameraController.getPosition(), 15.0f + glm::linearRand(0.5f, 1.5f), glm::abs(glm::sphericalRand(1.0f)), 30.0f });
            spawnedLightGeom.push_back(world.spawn("sphere", Transform(cameraController.getPosition(), 0.1f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_4 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            spawnedLights.push_back(SpotLight{ cameraController.getPosition(), 12.5f, glm::vec3(1.0f, 0.95f, 0.8f), 20.0f, cameraController.getDirection(), glm::radians(25.0f), 0.25f });
            glm::quat orientation = cameraController.getOrientation();
            const glm::vec3 camUp = cameraController.getUp();
            const glm::vec3 camRight = glm::normalize(glm::cross(cameraController.getDirection(), camUp));
            orientation = glm::angleAxis(glm::radians(90.0f), camRight) * orientation;
            spawnedLightGeom.push_back(world.spawn("cone", Transform(cameraController.getPosition(), 0.1f, orientation)));
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_5 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            const glm::vec3 dir = cameraController.getDirection();
            const glm::vec3 camUp = cameraController.getUp();
            const glm::vec3 up = glm::normalize(camUp);
            const glm::vec3 ref = glm::abs(up.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 right0 = glm::normalize(glm::cross(up, ref));
            const glm::vec3 camRight = glm::normalize(glm::cross(dir, camUp));
            float rotation = atan2f(glm::dot(glm::cross(right0, camRight), up), glm::dot(right0, camRight));
            spawnedLights.push_back(AreaLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 1.0f, 1.0f), 10.0f, camUp, 1.0f, 1.0f, rotation });

            const glm::quat orientation = glm::angleAxis(glm::radians(-90.0f), camRight) * cameraController.getOrientation();
            spawnedLightGeom.push_back(world.spawn("plane", Transform(cameraController.getPosition(), 0.5f, orientation)));
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_6 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            const glm::vec3 dir = cameraController.getDirection();
            const glm::vec3 camUp = cameraController.getUp();
            const glm::vec3 up = glm::normalize(camUp);
            const glm::vec3 ref = glm::abs(up.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 right0 = glm::normalize(glm::cross(up, ref));
            const glm::vec3 camRight = glm::normalize(glm::cross(dir, camUp));
            const float rotation = atan2f(glm::dot(glm::cross(right0, camRight), up), glm::dot(right0, camRight));
            spawnedLights.push_back(TubeLight{ cameraController.getPosition(), 10.0f, glm::vec3(1.0f, 0.9f, 0.7f), 10.0f, camUp, 0.1f, 1.0f, rotation });
            const glm::quat orientation = glm::angleAxis(glm::radians(-90.0f), camRight) * cameraController.getOrientation();
            spawnedLightGeom.push_back(world.spawn("capsule", Transform(cameraController.getPosition(), 0.5f, orientation)));
        }

        // 0: hang a chain of physics cubes in front of the camera (spherical joints, anchored to the world)
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_0 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            const glm::vec3 anchor = cameraController.getPosition() + cameraController.getDirection() * 2.0f;
            PhysicsBody* prevBody = &Globals::physics.staticBody();
            glm::vec3 pivot = anchor;
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 pos = anchor - glm::vec3(0.0f, 0.35f * float(i) + 0.175f, 0.0f);
                EntityPtr e = world.spawn("physicsCube", Transform(pos, 0.25f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
                PhysicsComponent* pc = e ? getComponent<PhysicsComponent>(e) : nullptr;
                if (!pc || !pc->body.isValid())
                    break;
                spawnedJoints.push_back(Globals::physics.createSphericalJoint(*prevBody, pc->body, pivot));
                prevBody = &pc->body;
                pivot = pos - glm::vec3(0.0f, 0.175f, 0.0f);
                world.addRootEntity(std::move(e));
            }
        }
        // 8/9: throw a dynamic physics cube/sphere from the camera
        if ((evt.scancode == SDL_Scancode::SDL_SCANCODE_8 || evt.scancode == SDL_Scancode::SDL_SCANCODE_9) && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            const bool cube = evt.scancode == SDL_Scancode::SDL_SCANCODE_8;
            const glm::vec3 dir = cameraController.getDirection();
            const Transform spawnAt(cameraController.getPosition() + dir, 0.25f, glm::normalize(cameraController.getOrientation()));
            if (EntityPtr e = world.spawn(cube ? "physicsCube" : "physicsSphere", spawnAt))
            {
                if (PhysicsComponent* pc = getComponent<PhysicsComponent>(e))
                    pc->body.setLinearVelocity(dir * 12.0f);
                world.addRootEntity(std::move(e));
            }
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_7 && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            for (int i = 0; i < 100; ++i)
            {
                int x = 0; int y = 0;
                glm::vec3 position = glm::vec3(x * 30.0f + glm::linearRand(-11.0f, 11.0f), glm::linearRand(0.0f, 7.0f), y * 20.0f + glm::linearRand(-5.0f, 4.5f));
                spawnedLights.push_back(PointLight{ position,
                    glm::linearRand(0.5f, 2.0f), glm::abs(glm::sphericalRand(1.0f)), glm::linearRand(7.0f, 10.0f) });
                spawnedLightGeom.push_back(world.spawn("sphere", Transform(position, 0.05f, glm::normalize(glm::quat(1.0, 0.0, 0.0, 0)))));
            }
        }
    }
};