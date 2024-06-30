module Core.Window;

import Core;
import Core.SDL;

bool Window::initialize(std::string_view windowTitle, glm::ivec2 pos, glm::ivec2 size)
{
	// initialize sdl and create a window
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
	{
		printf("SDL_Init Error: %s\n", SDL_GetError());
	}

	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, windowTitle.data());
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, pos.x);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, pos.y);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, size.x);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, size.y);
	SDL_SetNumberProperty(props, "flags", SDL_WINDOW_VULKAN);
	m_windowHandle = SDL_CreateWindowWithProperties(props);

	//m_windowHandle = SDL_CreateWindow(windowTitle.data(), size.x, size.y, SDL_WINDOW_VULKAN);
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