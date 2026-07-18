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
import Core.Tweaks;

import Animation;
import File;
import Input;
import UI;
import RendererVK;
import Entity;
import Script;
import Physics;
import Audio;
import Force;

export class InputControls
{
private:

    GizmoController& gizmo;
    FreeFlyCameraController& cameraController;
    World& world; // spawns go through the world's API; it owns the root entity lifetimes
    std::vector<RendererVKLayout::LightInfo>& spawnedLights;
    std::vector<EntityPtr>& spawnedLightGeom;
    std::vector<PhysicsJoint>& spawnedJoints;
    std::vector<ForceEmitter> spawnedForceEmitters; // test forcefields (keys N/M/H/B); RAII, cleared before globals die
    std::vector<ForceQuery> spawnedForceQueries;    // territory-query test grid (key J)
    struct ForceBall // physics body carrying a forcefield: enemy bubbles knock it back (key F)
    {
        EntityPtr entity;
        ForceEmitter emitter;
    };
    std::vector<ForceBall> forceBalls;
    KeyboardListener* pKeyboardListener;
    
    float output = 1.0f;          // must exceed the iso threshold (default 0.15) or no bubble exists
    float reach = 4.0f;           // TOTAL extent: the bubble spans pos .. pos + dir * reach
    float focus = 0.5f;           // shape pinch: 0.5 = sphere spanning the line, 0 = cone pointed at
                                  // the emitter, 1 = cone pointed at the target
    int32 team = 0;
    float distribution = 0.5f;    // where the output density sits along the line (0 = emitter end,
                                  // 1 = target end); budget-conserving bump
    float width = 1.0f;           // lateral scale (reach untouched): 1 = round, < 1 = narrower/sharper

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
		Tweak::floatVar("Force/Emitter", "Output", &output, 0.2f, 10.0f, 0.01f); // stay above iso (0.15)
		Tweak::floatVar("Force/Emitter", "Reach", &reach, 0.1f, 100.0f, 0.1f);
		Tweak::floatVar("Force/Emitter", "Focus", &focus, 0.0f, 1.0f, 0.01f);
		Tweak::intVar("Force/Emitter", "Team", &team, 0, 7, 1);
		Tweak::floatVar("Force/Emitter", "Distribution", &distribution, 0.0f, 1.0f, 0.01f);
		Tweak::floatVar("Force/Emitter", "Width", &width, 0.05f, 2.0f, 0.01f);

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

    // Per-frame maintenance of the test force-balls: the emitter follows the simulated body and the
    // GPU-read-back applied force (opposing bubbles pressing on it) feeds back as a physics impulse.
    // Call after world.update (body poses synced), before forceSystem.update pushes emitter state.
    void update(float deltaSec)
    {
        for (ForceBall& ball : forceBalls)
        {
            if (!ball.entity || !ball.emitter.isValid())
                continue;
            PhysicsComponent* pc = getComponent<PhysicsComponent>(ball.entity);
            if (!pc || !pc->body.isValid())
                continue;
            ball.emitter.setPosition(pc->body.getPosition() - glm::vec3(0, 1, 0)); // keep the span centered on the ball
            const glm::vec3 force = ball.emitter.getAppliedForce();
            const float pressure = ball.emitter.getPressure();
            if (glm::dot(force, force) > 1e-8f)
                pc->body.applyImpulse(force * deltaSec * 40000.0f * pressure);
        }
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
        if ((evt.scancode == SDL_Scancode::SDL_SCANCODE_N)
            && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN && !evt.repeat)
        {
            const glm::vec3 dir = cameraController.getDirection();
            const glm::vec3 pos = cameraController.getPosition() - (dir * reach * 0.5f);
            ForceEmitter emitter = Globals::forceSystem.createEmitter(team, pos, dir, output, reach, focus, distribution, width);
            spawnedForceEmitters.push_back(std::move(emitter));
        }
        if ((evt.scancode == SDL_Scancode::SDL_SCANCODE_M)
            && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN && !evt.repeat)
        {
			if (spawnedForceEmitters.empty())
				return;
			ForceEmitter& emitter = spawnedForceEmitters.back();
			emitter.setOutput(output);
			emitter.setReach(reach);
			emitter.setFocus(focus);
			emitter.setTeam(team);
			emitter.setDistribution(distribution);
			emitter.setWidth(width);
        }
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_B && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            spawnedForceEmitters.clear();
            spawnedForceQueries.clear();
            forceBalls.clear();
        }
        // H: forcefield stress burst — 500 random emitters (random team/reach/focus) in a 150 m disc
        // around the camera. Press repeatedly to stack toward the 5000-emitter target; B clears.
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_H && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            const glm::vec3 center = cameraController.getPosition();
            for (int i = 0; i < 500; ++i)
            {
                const glm::vec2 disc = glm::diskRand(150.0f);
                const glm::vec3 pos = center + glm::vec3(disc.x, glm::linearRand(-4.0f, 12.0f), disc.y);
                const uint32 team2 = (uint32)glm::linearRand(0.0f, 3.99f);
                const float focus2 = glm::linearRand(0.0f, 1.0f) < 0.25f ? glm::linearRand(0.7f, 1.0f) : 0.5f;
                spawnedForceEmitters.push_back(Globals::forceSystem.createEmitter(team2, pos,
                    glm::sphericalRand(1.0f), glm::linearRand(0.5f, 1.5f), glm::linearRand(1.5f, 5.0f), focus2));
            }
        }
        // F: throw a physics sphere carrying a small team-1 forcefield — enemy bubbles knock it back
        // via the GPU force readback (see update()).
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_F && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            const glm::vec3 dir = cameraController.getDirection();
            const Transform spawnAt(cameraController.getPosition() + dir, 0.25f, glm::normalize(cameraController.getOrientation()));
            if (EntityPtr e = world.spawn("physicsSphere", spawnAt))
            {
                if (PhysicsComponent* pc = getComponent<PhysicsComponent>(e))
                    pc->body.setLinearVelocity(dir * 12.0f);
                ForceBall ball;
                // Sphere spanning pos .. pos + dir * reach: offset the emitter down so it CENTERS on the ball.
                ball.emitter = Globals::forceSystem.createEmitter(1u, spawnAt.pos - glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), 1.0f, 2.0f);
                ball.entity = e;
                forceBalls.push_back(std::move(ball));
                world.addRootEntity(std::move(e));
            }
        }
        // J: drop an 11x11 territory-query grid at the camera's height in front of it ("Force/Debug/
        // Draw queries" colors each point by the team whose bubble contains it, grey = uncontrolled).
        if (evt.scancode == SDL_Scancode::SDL_SCANCODE_J && evt.type == SDL_EventType::SDL_EVENT_KEY_DOWN)
        {
            const glm::vec3 center = cameraController.getPosition() + cameraController.getDirection() * 10.0f;
            for (int x = -5; x <= 5; ++x)
                for (int z = -5; z <= 5; ++z)
                    spawnedForceQueries.push_back(Globals::forceSystem.createQuery(center + glm::vec3(x * 2.0f, 0.0f, z * 2.0f)));
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