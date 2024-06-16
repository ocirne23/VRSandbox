module;

#include <glm/vec2.hpp>
#include <string_view>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

module Core.Window;

Window::Window()
{
}
/*

Window::Window(std::string_view windowTitle, glm::ivec2 pos, glm::ivec2 size)
{
	// initialize sdl and create a window
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		printf("SDL_Init Error: %s\n", SDL_GetError());
	}
	m_windowHandle = SDL_CreateWindow(windowTitle.data(), pos.x, pos.y, size.x, size.y, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
	if (m_windowHandle == nullptr)
	{
		printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
	}
	assert(m_windowHandle);
}*/

Window::~Window()
{
}