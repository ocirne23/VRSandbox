# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# Project Description
A modern c++ Game Engine codebase. Windows/MSVC only. The `App` executable is the testbed: it spawns `Entities/SponzaScene.pre` (Sponza + skysphere prefabs, Sponza with a static mesh collider) and `Entities/character.pre` (skinned, animated, scripted character) through the `World`, runs an ImGui editor UI (scene hierarchy, properties, asset browser, visual-script node editor, tweaks) around the viewport, and drives a transform gizmo. VR is initialized with `EVr::DISABLED` in main.cpp — flip to `ENABLED` to run on an OpenXR headset. A Bistro scene (`Entities/BistroScene.pre`) exists as an alternative test scene.

## App testbed key bindings
* F5 = shader hot-reload, F6 = recompile + hot-swap the current visual script
* T/R/G = gizmo translate/rotate/scale
* P = GI probe debug cubes, O = cycle probe debug mode, L = aim sun along camera
* 1 = clear spawned lights, 2/3 = point lights, 4 = spot, 5 = area, 6 = tube, 7 = 100 random point lights
* 8/9 = throw physics cube/sphere, 0 = hang a physics chain (spherical joints)
* WASD/LShift also fire global script events ("W Down"/"W Up", ...) via `Globals::scriptEvents`; Ctrl+C/V = script node copy/paste

## Building
* CMake with Visual Studio 18 2026 generator, build dir is `Build/`
	* Reconfigure: `cmake -B Build` (only needed when files are added/removed — all sources are globbed, never edit CMakeLists to add files)
	* Build: `cmake --build Build --config Debug` (also `RelWithDebInfo`, `Release`)
	* Output: `Build/x64/<Config>/App.exe`. Run from anywhere inside the repo; `FileSystem::initialize()` walks up to find `Assets/` and sets it as the working directory, so all asset paths are relative to `Assets/`
* No test suite, no linter. Verifying = it compiles + the user runs it
* Prefer to let me test out changes myself rather than looking at log output/screen yourself
* Dependencies are prebuilt in `Dependencies/` (Include/Lib/Dll) — Vulkan-Hpp (no exceptions, no constructors), SDL3, ImGui docking branch, Assimp, glslang/shaderc, OpenXR loader, Nsight Aftermath. Exception: box3d is vendored source (`Dependencies/box3d`), built via `add_subdirectory` from `Dependencies/CMakeLists.txt` — added in the root CMakeLists before `add_definitions` so its C sources skip forceinclude.h, with its MSVC runtime overridden to the DLL CRT

## Style
* Using exclusively c++20 modules instead of headers. (.ixx instead of .h)
	* Most libraries use module partitions: `export module RendererVK:Renderer;` in the interface .ixx, `module RendererVK:Renderer;` in the implementation .cpp; public surface re-exported from `Public/<Lib>.ixx` (`RendererVK`, `Entity`, `Animation`, `File`, `Input` all do this)
	* `Core` and `UI` use dotted module names instead (`Core.Tweaks`, `UI.NodeEditor.Scene`, one module per .ixx)
	* `Public/` folder .ixx's are importable from outside the library; `Private/` is internal
	* The `Core` module re-exports std headers as header units — if you need a std header that isn't available, add it to `Code/Core/Public/Core.ixx`
	* `Core.fwd` / `*.fwd.ixx` modules hold forward declarations
	* One exception: `Script/Public/ScriptAPI.h` is a plain header — it's the ABI shared with runtime-compiled script DLLs and is `#include`d on both sides
* Modernest conventions
	* No new/delete, only memory safe types.
	* Raw pointers are non-owning
	* Minimal comments, only in exceptional cases where code intent is too difficult to communicate
	* Prioritize performance
* Engine singletons live in `export namespace Globals` inside the owning .ixx (`Globals::rendererVK`, `Globals::device`, `Globals::input`, `Globals::ui`, `Globals::world`, `Globals::physics`, `Globals::scriptHost`, `Globals::scriptEvents`, `Globals::assetRegistry`, `Globals::time`, `Globals::allocator`, ...). RendererVK globals use `#pragma init_seg` to control construction order — keep that when adding one. Globals in *different* libraries have no defined construction order relative to each other (see `ScriptEventManager::initialize` for the pattern when one global must register with another)
* int8/uint64-style typedefs from `Core`; glm via `Core.glm`; `assert` is compiled out in non-debug via forceinclude.h

# Projects (folders/libraries)
Dependency direction (`target_link_libraries`, all public): everything imports Core. `Animation`, `Physics`, `Script` depend only on Core; `File` imports Animation; `RendererVK` imports File; `Entity` imports RendererVK + File + Script + Physics; `UI` imports Entity + RendererVK; `Input` imports UI; `App` links all.

## Core
* Collection of cross library utility classes and functions
* Wrappers for bigger headers (glm, SDL, imgui, Windows, Vulkan)
* Shared functionality like memory allocation (`Allocator`, global new/delete routed through it), `JobSystem` (coroutine based), `Log` (ring buffer the UI Log panel snapshots), `Time`/`Timer`, math types (`Transform`, `Frustum`, `AABB`, `Sphere`, `Rect`, `Camera` — `Camera::screenToWorld` for viewport picking)
* Containers/utilities: `LPMultiMap` (linear-probing multimap), `LockFreeList`, `ExpiryCache`, `BitRangeAllocator`, `CheckedPtr`/`RefCheckable` (debug-checked non-owning pointers)
* `Core.VrSession`: `IVrSession` interface (opaque `void*`/int64 handles, no OpenXR types) — implemented by RendererVK's `OpenXRSession`, consumed by Input's `VrInput`, so Input and RendererVK stay unlinked
* Tweaks module (`Core.Tweaks`): call `Tweak::floatVar/intVar/color3/...("Category/Sub", "Name", &liveVariable, ...)` once at init to expose a variable in the UI TweakPanel. Pointers are non-owning, variable must outlive registration. This is the standard way to make anything runtime-configurable

## RendererVK
* Modern Vulkan renderer designed to only use modern features (minimum spec RTX 2000 series)
* Device Generated Commands (DGC) for everything
* Lights culled with a world space hash table indexing into grids with a cellcount based on distance to camera
* Global illumination using scrolling clipmap radiance cascades (ray traced probes)
* RTAO, TAA, PCSS cascades, volumetric fog, GPU compute skinning
* Everything upstream of composite is linear HDR: `EyeAdaptationPipeline` (log-luminance histogram + smoothed auto-exposure, compute) feeds `CompositePipeline` (fullscreen HDR→display pass into the swapchain before ImGui; tonemap operators: off/Reinhard/ACES/AgX)
* Frame order (see `recordCommandBuffers()` / `present()`): skinning compute → indirect cull → light grid → shadow cull + draw → static mesh/G-buffer → GI probes → RTAO → volumetric fog → TAA → eye adaptation → composite + ImGui
* Layer structure under `Private/`:
	* `Objects/` — thin Vulkan object wrappers (Device, SwapChain, Buffer, Pipeline, AccelerationStructure, GBuffer, SceneColor, ShadowMap, ...)
	* `Pipeline/` — one class per render pass/feature (GBuffer, GIProbe, RTAO, TAA, VolumetricFog, EyeAdaptation, Composite, Skinning, light grid + indirect/shadow culling compute, shadows). Each registers its own Tweak vars
	* `Data/` — MeshDataManager, TextureManager, StagingManager, ShaderDatabase (Aftermath)
	* `Renderer.ixx/.cpp` — orchestrates everything; per-pass `record*()` methods called from `recordCommandBuffers()`
	* `OpenXRSession.ixx` — VR: OpenXR session/swapchain wrapper (`Globals::openXR`, implements `IVrSession`). Two-phase init around Vulkan device creation (runtime dictates instance/device extensions + physical device); stereo rendering targets a 2-layer multiview swapchain; in VR the editor viewport sub-rect is ignored (full-extent render). `Renderer::initialize` takes `EVr::ENABLED/DISABLED`
* Scene objects: an `ObjectContainer` wraps one loaded `ISceneData` (meshes/materials/textures, optional `MaterialOverrides`); `container.spawnNodeForIdx()` returns a `RenderNode` instance handle (RAII, movable), `spawnSkinnedNode()` the GPU-skinned variant (an `AnimatorComponent` feeds its bone palette via `allocateSkinningPalette`/`setSkinningPalette`). Lights are added per-frame via `renderer.addPointLight/addSpotLight/addAreaLight/addLightInfo/...`
* Try to record CommandBuffers once (not per frame) whenever possible; call `setHaveToRecordCommandBuffers()` after changes that invalidate them
* Shaders are GLSL in `Assets/Shaders/`, compiled at runtime with glslang (so shader edits don't need a rebuild — F5 in the app calls `reloadShaders()`). `*.inc.glsl` files are includes; `shared.inc.glsl`/`ubo.inc.glsl` define structs that must stay in sync with `Private/Layout.ixx` (`RendererVKLayout`). Compiled SPIR-V + source dumps land in `Assets/Local/` for Aftermath GPU crash analysis

## Entity
* The ECS. Inline component layout: an `Entity` (pos/scale/rot, parent, displayName, spawn template, refcount, `typeBits`) and its components live in ONE allocation — `typeBits` is a bitmask over the 7 component types (fixed order: Scene, Zone, Cull, Render, Animator, Script, Physics; `EComponentID` in `EntityDef.ixx`), and `getComponent<T>(entity)` / `hasComponent<T>` compute the byte offset from it at compile-time-known sizes (`Component.ixx`). Adding a component type means: new struct with `getId()`, entry in `EComponentID` + `inlineSizes` + `componentTypeName` + create/destroy/serialize switches in Entity.cpp, bump `MaxInlineComponentTypes`
* `EntityPtr`: intrusive refcounted handle (atomic on `refCount`); `Entity::create(template, transform)` placement-news into `Globals::entityAllocator`. Entities form a tree via `SceneComponent::children` + `Entity::parent`; `reparentEntity` moves ownership
* `Entity::update()` → `updateTree`: per entity runs ScriptComponent → AnimatorComponent → PhysicsComponent, composes the world transform, pushes the RenderComponent's `RenderNode` to the renderer, recurses into scene children. Disabled `SceneComponent` prunes the subtree
* `World` (`Globals::world`): `spawn("prefabName", transform)`, `spawnAssetFile("Entities/x.pre", ...)`, `createEmptyEntity`. Builds and caches `EntitySpawnTemplate`s (archetype + per-component `SpawnInfo` recipes) from parsed assets; caches `ObjectContainer`s, collision sources/meshes, and retargeted animation clip sets so each source file is imported once. `reloadPrefabs`/`invalidatePrefab` for editing (retired templates stay alive for live entities)
* `AssetRegistry` (`Globals::assetRegistry`): scans `Assets/` at startup and registers everything by name — `ObjectContainer` + `StaticMesh`/`SkinnedMesh` spawnables (.oc), `Animation` clips (.anm), `Animator` graphs (.apl), `Prefab`s (.pre)
* Prefabs: `savePrefab(root, path)` serializes an entity tree back to .pre (per-component `serialize`/`writeRenderSpawnInfo`/...); spawned prefabs are locked instances (`EEntityFlag_PrefabInstance`, unpackable in the Scene panel); `prefabWouldCycle` guards recursive saves
* Components: `SceneComponent` (children + enabled), `RenderComponent` (RenderNode + local transform, static or skinned), `AnimatorComponent` (AnimationPlayer + AnimStateMachine from a .apl, ticks and pushes the skinning palette; gameplay via `stateMachine.setFloat/Bool/Trigger`, clip events via `onEvent`), `ScriptComponent` (runs a .scr each frame with the entity as `self`), `PhysicsComponent` (see Physics), `ZoneComponent`/`CullingComponent` (mostly placeholders; `Zone.ixx` holds Morton-code entity-hash experiments)
* Script glue lives here, not in Script: `Globals::scriptContext` (the `ScriptContext` ABI singleton + host thunks), `ScriptEventManager` (`Globals::scriptEvents`) — global named events (`fireEvent("W Down")`), scripts subscribe by declaring On Event entries, mapping rebuilt on script reload. Its `initialize()` must run from main before any scripted entity spawns (cross-library global construction order). Deferred root-list mutations from scripts (`Globals::scriptRootAdditions/scriptRootRemovals/scriptDestroyRequests`) are drained by the App main loop each frame
* `dispatchPhysicsContactEvents()` (called from main after `physics.update`) maps contact/sensor events onto entities: invokes `PhysicsComponent::onContact` and fires script "Contact Begin"/"Contact End"/"Sensor Begin"/"Sensor End"

## Script
* Visual scripts compile to real DLLs: `ScriptHost` (`Globals::scriptHost`, depends only on Core) compiles a .scr with the installed MSVC toolchain (finds vcvars64.bat via vswhere, then `cl /LD /std:c++20 /MD /Od /Zi /DSCRIPT_BUILD`) into a self-contained DLL under `Assets/Local/Scripts/`, caches by path, hot-reloads on demand (`getOrLoad(path, forceRecompile)`) — F6 or the Script panel's Compile & Run; live entities hot-swap. On failure the previous build is kept; superseded PDBs are garbage-collected around the debugger
* A .scr file IS the generated C++ (`#include "ScriptAPI.h"`, entry points `OnSpawn`/`Update`/`OnDestroy`/`OnEvent` + `ScriptEventCount`/`ScriptEventName`/`ScriptDataSize`) with the node graph appended as `//@graph` / `//@node` metadata comments — the NodeEditor regenerates the code from the graph, and the file is also hand-editable C++
* `ScriptAPI.h` is the host↔DLL ABI: C linkage, PODs/glm/raw pointers only (no STL, no engine types). The `ScriptContext` function table is APPEND-ONLY (cached DLLs compiled against older layouts must keep working). Scripts see a layout-compatible `Entity` MIRROR struct (pos/scale/rot/parent only — fields past `std::string displayName` differ under the debug CRT); the engine static_asserts the offsets in ScriptContext.cpp so drift fails the build. Everything else goes through `ctx->entity*` functions (name, children, animation params, physics velocity/impulse/raycast, spawn/destroy, events)
* `ScriptModule`: the loaded DLL's entry-point pointers + declared event names; don't cache the function pointers across reloads

## Physics
* Box3D (Erin Catto's 3D engine, C API) wrapped in `module Physics`; depends only on Core, box3d is linked PRIVATE and never leaks into the public surface (body/joint/mesh ids stored as integer bits)
* `Globals::physics` (`PhysicsWorld`): `initialize()` once, `update(deltaSec)` per frame (fixed-step accumulator; gravity/paused/interpolate/timescale/substeps/Hz are Tweak vars under Physics/World), `createBody(PhysicsBodyDesc, span<PhysicsShape>)` (Box/Sphere/Capsule/Hull/Mesh; the desc transform's scale is baked into shape dims), `castRayClosest()`
* `PhysicsBody`/`PhysicsJoint`/`PhysicsMesh`: RAII movable handles, like RenderNode. Joints (`createDistance/Revolute/Spherical/WeldJoint`) take world-space anchors/axes; `staticBody()` anchors joints to the world. `PhysicsMesh` (triangle BVH) is referenced by Mesh shapes and must outlive them; Hull shapes clone at creation. Mesh colliders only collide on static bodies
* `PhysicsComponent` in Entity: `Component Physics` in .pre files (`Body` Static/Kinematic/Dynamic, `Shape` Box/Sphere/Capsule/Hull/Mesh, `HalfExtents`/`Radius`/`HalfHeight`/`Offset`/`Density`/`Friction`/`Restitution`/`MaxHullVertices`/`Sensor`/`ContactEvents`, see `Assets/Entities/Debug/physicsCube.pre`). Hull/Mesh shapes source geometry from the sibling render mesh: a `CollisionSource` snapshot (positions/indices + node tree) is captured from the same `ISceneData` the container loads from — one import, not two (re-import only as fallback when the container was loaded before physics needed it). Meshes/nodes named `Col_*` are collision proxies: physics collides with them instead of the same-named render mesh (`Col_Wall` replaces `Wall`; meshes without a proxy collide as themselves) and the renderer never instances them (skipped in `ObjectContainer::initializeNodes`). Dynamic bodies write the simulated pose back into the entity's local transform each update, interpolated between fixed steps; kinematic/static bodies follow the entity when it's moved (gizmo/scripts). Sponza has a static mesh collider (sponza.pre)
* Contact/sensor events: shapes opt in via `ContactEvents`/`Sensor`; `PhysicsWorld::update` drains them per step into `getContactEvents()` (body userData pairs); `dispatchPhysicsContactEvents()` (Entity, called from main loop) invokes `PhysicsComponent::onContact` and fires script On Event entries "Contact Begin"/"Contact End"/"Sensor Begin"/"Sensor End"
* Collision masking via named layers: `PhysicsLayers::bit(name)` allocates one of 64 category bits on first use ("Default" = bit 0); `PhysicsShape` carries `categoryBits`/`maskBits`/`groupIndex`. In .pre: `Layer Debris` + `CollidesWith Default, Player` (or `All`/`None`) + optional `Group <int>`; two shapes collide when each one's mask contains the other's category. `castRayClosest` takes an optional layer mask (default all). Demo: physicsSphere.pre spheres pass through each other

## Animation
* Skeletal animation runtime (depends only on Core). Partitioned module `Animation` (`import Animation;`): `Animation:Skeleton` (`Skeleton` bone hierarchy), `Animation:Clip` (`AnimationClip`/`AnimationSet` keyframes + `AnimationPlayer`: CPU sampler producing a bone-matrix palette, with crossfade blending, 1D `BlendSpace1D`, and programmatic per-bone posing via `setBoneTransform`/`setBoneOffset`), and `Animation:StateMachine` (`AnimStateMachine`: parameter-driven states/transitions that crossfade an `AnimationPlayer`)
* File builds `Skeleton`/`AnimationClip` from Assimp; RendererVK consumes the palette for GPU skinning; Entity's `AnimatorComponent` + .apl assets drive it data-driven (World caches retargeted clip sets per skeleton+animator)

## File
* Wrappers for assets and file system related functionality
* `ISceneData::createAssimpLoader()` for 3d model files (Assimp), `ISceneData::createProceduralLoader()` to programmatically generate assets (terrain, skysphere, debug shapes)
* `AssetParser` (`File:AssetParser`): the text format behind all engine text assets (.pre/.oc/.anm/.apl). Indentation-defined hierarchy of `AssetNode` (key + values + children), `#`/`//` comments, quoted strings, comma-separated vectors; `loadAssetFile`/`parseAssetText`/`writeAssetText`, case-insensitive `find`, typed accessors with fallbacks
* `FileSystem` sets the working directory to `Assets/` at startup

## UI
* ImGui library for all UI utilizing the Docking branch for windows; `UI::update` runs the panels, panel mutations flow back to the App as an `EntityChange` queue (`takeEntityChanges()`: create from asset/viewport-drop, add empty, delete, reparent, save prefab) — the UI never mutates the world directly
* Panels:
	* Viewport — renderer output; assets can be drag-dropped from Content to spawn at the cursor (`Camera::screenToWorld`)
	* Scene (`SceneView`) — live hierarchy: selection, F2 rename, Delete, drag-reparent, drag-to-Content to save a prefab, context menus, prefab instances shown locked (unpackable)
	* Properties (`PropertiesPanel`) — edit selected entity's name/enabled/pos/scale/rot, render component info + bounds toggle, script path
	* Content (`AssetBrowser`) — filesystem browser rooted at `Assets/`: grid/list, drag assets into viewport/hierarchy, accept prefab drops (overwrite/cycle guards), create/rename .scr files, context menus
	* Script (`NodeEditor::Scene` + imgui-node-editor) — the visual scripting frontend: categorized node palette (spawn submenus, link-drag spawning, pin redirect), comment boxes, waypoints, copy/paste, generates C++ (`generateCpp`) and saves .scr (code + `//@graph` metadata), Compile & Run requests drained by main (`takeScriptReloadRequests`), follows the hierarchy selection to open its entity's script (unsaved-changes prompt)
	* Log (`OutputLog`) — console view of `Core.Log` with level filters/search
	* Tweaks (`TweakPanel`) — auto-populated from the Tweak registry
	* Stats — renderer statistics (`ui.setRenderStats`)

## Input
* SDL3 event pump; listener objects (`addKeyboardListener()`, `addMouseListener()`, `addSystemEventListener()`) with std::function callbacks
* `GizmoController` (App-owned, in Input): spawns `Entities/Gizmo.pre` and drives it — follows the Scene panel selection, constant apparent screen size, handle set switches with `EGizmoMode` Translate/Rotate/Scale (T/R/G), mouse-drag manipulation writes back to the entity (kinematic/static physics bodies follow)
* `VrInput` (`Globals::vrInput`): OpenXR controller input (thumbsticks/poses/buttons), wired via the shared `Core.VrSession` `IVrSession` interface the renderer implements (no Input↔Renderer link)
* Camera controllers: `FreeFlyCameraController` (desktop WASD+mouse) and `VRFreeFlyCameraController` (thumbstick play-space locomotion); both expose `getCamera()`. App picks by `renderer.isVrEnabled()`

# Asset files (all text formats parsed via `AssetParser`, registered by `AssetRegistry`)
* `.oc` (ObjectContainer): a model source + import options (`Path`, `MergeNodes`, `PreTransformVertices`, `MaterialOverrides` with `PipelineIdx`/`ExcludeFromRayTracing`/texture overrides) plus named `StaticMesh`/`SkinnedMesh` spawnable entries (`Node` path within the model; SkinnedMesh adds `Type Humanoid/Generic` + optional bone mapping). Skinned containers MUST keep `PreTransformVertices false`
* `.pre` (Prefab): a named entity (hierarchy) — `Prefab <name>` root with `Component Render/Animator/Script/Physics/Scene` children; `Component Scene` nests child `Prefab` references. Render references a spawnable (`StaticMesh <name>`) or `ObjectContainer <name>` + `Node`, plus `Position/Rotation/Scale`. Spawn by prefab name (`world.spawn("physicsCube", ...)`) or file path (`world.spawnAssetFile`)
* `.anm`: `Animation <name>` clip entries — source `ObjectContainer` (fbx), `Loop`, `Skip` (channels), `Event <name> <normalizedTime>` notifies (fired into the entity's script/animator `onEvent`)
* `.apl`: `Animator <name>` graph — `Parameter`s (Float/Bool/Trigger), `Clip` bindings, `BlendSpace1D` with `Sample`s, `StateMachine` (states with `Play`/`SpeedParam`/`SpeedScale`, `Transition`/`AnyTransition` with `Condition`/`ExitTime`/`Fade`)
* `.scr`: visual script = generated C++ + `//@graph` node metadata (see Script)
* `Assets/Local/` is generated output (compiled SPIR-V + shader source dumps for Aftermath, compiled script DLLs/PDBs) — never hand-edit
