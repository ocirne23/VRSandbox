module Core.Window;

import Core;
import Core.SDL;

//import <EASTL/vector.h>;

bool Window::initialize(std::string_view windowTitle, glm::ivec2 pos, glm::ivec2 size)
{
    // initialize sdl and create a window
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        printf("SDL_Init Error: %s\n", SDL_GetError());
    }

    m_windowHandle = SDL_CreateWindow(windowTitle.data(), size.x, size.y, SDL_WINDOW_VULKAN);
    if (m_windowHandle == nullptr)
    {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        assert(false);
        return false;
    }
    return true;
}

void Window::setTitle(std::string_view title)
{
    SDL_SetWindowTitle((SDL_Window*)m_windowHandle, title.data());
}

void Window::getWindowSize(glm::ivec2& size) const
{
    SDL_GetWindowSizeInPixels((SDL_Window*)m_windowHandle, &size.x, &size.y);
}

Window::Window()
{
}

Window::~Window()
{
}