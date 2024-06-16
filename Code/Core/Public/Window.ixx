module;

#include <string_view>
#include <glm/vec2.hpp>

export module Core.Window;

export class Window
{
public:
	Window();
	~Window();
	Window(const Window&) = delete;

	bool initialize(std::string_view windowTitle, glm::ivec2 pos, glm::ivec2 size);

	void* getWindowHandle() const { return m_windowHandle; }
	void setTitle(std::string_view title);

private:
	void* m_windowHandle = nullptr;
};