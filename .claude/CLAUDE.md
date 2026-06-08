# Project Description
A modern c++ Game Engine codebase.
## Style
* Using exclusively c++20 modules instead of headers. (.ixx instead of .h)
	* Using Module partitions within each library
	* Public folder .ixx's for things accessible outside the library
* Modernest conventions
	* No new/delete, only memory safe types.
	* Raw pointers are non-owning
	* Minimal comments, only in exceptional cases where code intent is too difficult to communicate
	* Prioritize performance
### Building
* CMake project
	* Visual Studio 26 as generator
	* When new files are added, reconfigure cmake
	* Build using CMake
* Prefer to let me test out changes myself rather than looking at log output/screen yourself

# Projects (folders/libraries)
## Core
* Collection of cross library utility classes and functions
* Wrappers for bigger headers
* Shared functionality like memory allocation and containers
* Provides the "Core" module which every .ixx has to import
	* Any std library functions you need can be added to this file
## Renderer
* Modern Vulkan renderer designed to only use modern features (minimum spec RTX 2000 series) 
* Device Generated Commands (DGC) for everything
* Lights culled with a world space hash table indexing into grids with a cellcount based on distance to camera
* Global illumination probes using hardware raytracing
	* Also using hashtable lookup for probe data
* Try to record CommandBuffers once (not per frame) whenever possible
## UI
* ImGui library for all UI utilizing the Docking branch for windows
* Couple of panels
	* Viewport (Renderer output)
	* Script (WIP visual scripting thing)
	* Scene (Scenegraph hierarchy display, dummy implementation)
	* Properties (Selected scenegraph item info, dummy implementation)
	* Content (Asset browser, displays filesystem, but can't use for anything yet)
	* Log (Console output, dummy implementation)
	* Stats (Random renderer statistics)
## Entity
* Intended to implement an Entity-Component-System, but mostly unimplemented and completely unused at this point
## Scene
* Intended to manage the scenegraph, but completely unimplemented at this point
## File
* Wrappers for assets and file sytem related functionality
* Assimp folder for 3d model file loading using the Assimp library
* Procedural folder to programmatically generate assets (mostly for debug info)
## Input
* Does input related things