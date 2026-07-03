# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# Project Description
A modern c++ Game Engine codebase. Windows/MSVC only. The `App` executable is the testbed: it loads Sponza plus debug shapes and a procedural sky sphere, and binds keys for testing (F5 = shader hot-reload, P/O = GI probe debug, L = aim sun along camera, 1-7 = spawn/clear lights, 8/9 = throw physics cubes/spheres).

## Building
* CMake with Visual Studio 18 2026 generator, build dir is `Build/`
	* Reconfigure: `cmake -B Build` (only needed when files are added/removed — all sources are globbed, never edit CMakeLists to add files)
	* Build: `cmake --build Build --config Debug` (also `RelWithDebInfo`, `Release`)
	* Output: `Build/x64/<Config>/App.exe`. Run from anywhere inside the repo; `FileSystem::initialize()` walks up to find `Assets/` and sets it as the working directory, so all asset paths are relative to `Assets/`
* No test suite, no linter. Verifying = it compiles + the user runs it
* Prefer to let me test out changes myself rather than looking at log output/screen yourself
* `TestApp` is disabled (its CMakeLists starts with `return()`)
* Dependencies are prebuilt in `Dependencies/` (Include/Lib/Dll) — Vulkan-Hpp (no exceptions, no constructors), SDL3, ImGui docking branch, Assimp, glslang/shaderc, Nsight Aftermath. Exception: box3d is vendored source (`Dependencies/box3d`), built via `add_subdirectory` from `Dependencies/CMakeLists.txt` — added in the root CMakeLists before `add_definitions` so its C sources skip forceinclude.h, with its MSVC runtime overridden to the DLL CRT

## Style
* Using exclusively c++20 modules instead of headers. (.ixx instead of .h)
	* `RendererVK` uses module partitions: `export module RendererVK:Renderer;` in interface .ixx, `module RendererVK:Renderer;` in the implementation .cpp. Public surface re-exported from `Public/RendererVK.ixx`
	* Other libraries use dotted module names (`Core.Tweaks`, `File.FileSystem`, one module per .ixx)
	* `Public/` folder .ixx's are importable from outside the library; `Private/` is internal
	* The `Core` module re-exports std headers as header units — if you need a std header that isn't available, add it to `Code/Core/Public/Core.ixx`
	* `Core.fwd` / `*.fwd.ixx` modules hold forward declarations
* Modernest conventions
	* No new/delete, only memory safe types.
	* Raw pointers are non-owning
	* Minimal comments, only in exceptional cases where code intent is too difficult to communicate
	* Prioritize performance
* Engine singletons live in `export namespace Globals` inside the owning .ixx (`Globals::rendererVK`, `Globals::device`, `Globals::input`, `Globals::ui`, `Globals::time`, `Globals::allocator`, ...). RendererVK globals use `#pragma init_seg` to control construction order — keep that when adding one
* int8/uint64-style typedefs from `Core`; glm via `Core.glm`; `assert` is compiled out in non-debug via forceinclude.h

# Projects (folders/libraries)
Dependency direction: everything imports Core. Animation imports Core; File imports Animation; RendererVK imports File; UI imports RendererVK; Input imports UI; App links all.
## Core
* Collection of cross library utility classes and functions
* Wrappers for bigger headers (glm, SDL, imgui, Windows, Vulkan)
* Shared functionality like memory allocation (`Allocator`, global new/delete routed through it), containers, `JobSystem` (coroutine based), `Log`, `Time`, math types (`Transform`, `Frustum`, `AABB`, `Camera`)
* Tweaks module (`Core.Tweaks`): call `Tweak::floatVar/intVar/color3/...("Category/Sub", "Name", &liveVariable, ...)` once at init to expose a variable in the UI TweakPanel. Pointers are non-owning, variable must outlive registration. This is the standard way to make anything runtime-configurable
## RendererVK
* Modern Vulkan renderer designed to only use modern features (minimum spec RTX 2000 series)
* Device Generated Commands (DGC) for everything
* Lights culled with a world space hash table indexing into grids with a cellcount based on distance to camera
* Global illumination using scrolling clipmap radiance cascades (ray traced probes)
* RTAO, TAA, PCSS cascades, volumetric fog
* Layer structure under `Private/`:
	* `Objects/` — thin Vulkan object wrappers (Device, SwapChain, Buffer, Pipeline, AccelerationStructure, ...)
	* `Pipeline/` — one class per render pass/feature (GBuffer, GIProbe, RTAO, TAA, VolumetricFog, light grid + culling compute, shadows). Each registers its own Tweak vars
	* `Data/` — MeshDataManager, TextureManager, StagingManager, ShaderDatabase (Aftermath)
	* `Renderer.ixx/.cpp` — orchestrates everything; per-pass `record*()` methods called from `recordCommandBuffers()`
* Scene objects: an `ObjectContainer` wraps one loaded `ISceneData` (meshes/materials/textures, optional `MaterialOverrides`); `container.spawnNodeForIdx()` returns a `RenderNode` instance handle (RAII, movable). Lights are added per-frame via `renderer.addPointLight/addSpotLight/...`
* Try to record CommandBuffers once (not per frame) whenever possible; call `setHaveToRecordCommandBuffers()` after changes that invalidate them
* Shaders are GLSL in `Assets/Shaders/`, compiled at runtime with glslang (so shader edits don't need a rebuild — F5 in the app calls `reloadShaders()`). `*.inc.glsl` files are includes; `shared.inc.glsl`/`ubo.inc.glsl` define structs that must stay in sync with `Private/Layout.ixx` (`RendererVKLayout`). Compiled SPIR-V + source dumps land in `Assets/Local/` for Aftermath GPU crash analysis
## UI
* ImGui library for all UI utilizing the Docking branch for windows
* Couple of panels
	* Viewport (Renderer output)
	* Tweaks (panel to configure renderer and other things; auto-populated from the Tweak registry)
	* Script (WIP visual scripting thing, imgui-node-editor)
	* Scene (Scenegraph hierarchy display, dummy implementation)
	* Properties (Selected scenegraph item info, dummy implementation)
	* Content (Asset browser, displays filesystem, but can't use for anything yet)
	* Log (Console output, dummy implementation)
	* Stats (Random renderer statistics)
## Physics
* Box3D (Erin Catto's 3D engine, C API) wrapped in `module Physics`; depends only on Core, box3d is linked PRIVATE and never leaks into the public surface (body/world ids stored as integer bits)
* `Globals::physics` (`PhysicsWorld`): `initialize()` once, `update(deltaSec)` per frame (fixed-step accumulator; gravity/paused/timescale/substeps/Hz are Tweak vars under Physics/World), `createBody(PhysicsBodyDesc, span<PhysicsShape>)` (Box/Sphere/Capsule; the desc transform's scale is baked into shape dims), `castRayClosest()`
* `PhysicsBody`: RAII movable body handle, like RenderNode
* `PhysicsComponent` in Entity: `Component Physics` in .pre files (`Body` Static/Kinematic/Dynamic, `Shape` Box/Sphere/Capsule, `HalfExtents`/`Radius`/`HalfHeight`/`Offset`/`Density`/`Friction`/`Restitution`, see `Assets/Entities/Debug/physicsCube.pre`). Dynamic bodies write the simulated pose back into the entity's local transform each update; kinematic/static bodies follow the entity when it's moved (gizmo/scripts)
## Entity
* Intended to implement an Entity-Component-System, but mostly unimplemented and currently unused
## Scene
* Intended to manage the scenegraph, but completely unimplemented at this point
## Animation
* Skeletal animation runtime (depends only on Core). Partitioned module `Animation` (`import Animation;`): `Animation:Skeleton` (`Skeleton` bone hierarchy), `Animation:Clip` (`AnimationClip`/`AnimationSet` keyframes + `AnimationPlayer`: CPU sampler producing a bone-matrix palette, with crossfade blending, 1D `BlendSpace1D`, and programmatic per-bone posing via `setBoneTransform`/`setBoneOffset`), and `Animation:StateMachine` (`AnimStateMachine`: parameter-driven states/transitions that crossfade an `AnimationPlayer`)
* File builds `Skeleton`/`AnimationClip` from Assimp; RendererVK consumes the palette for GPU skinning (see skinned-mesh notes)
## File
* Wrappers for assets and file sytem related functionality
* `ISceneData::createAssimpLoader()` for 3d model files (Assimp), `ISceneData::createProceduralLoader()` to programmatically generate assets (terrain, skysphere, debug shapes)
* `FileSystem` sets the working directory to `Assets/` at startup
## Input
* SDL3 event pump; listener objects (`addKeyboardListener()`, `addMouseListener()`, `addSystemEventListener()`) with std::function callbacks
* `VrInput` (`Globals::vrInput`): OpenXR controller input (thumbsticks/poses/buttons), wired via the shared `Core.VrSession` `IVrSession` interface the renderer implements (no Input↔Renderer link)
* Camera controllers: `FreeFlyCameraController` (desktop WASD+mouse) and `VRFreeFlyCameraController` (thumbstick play-space locomotion); both expose `getCamera()`. App picks by `renderer.isVrEnabled()`
