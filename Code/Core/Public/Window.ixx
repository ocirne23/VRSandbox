export module Core.Window;

import Core;

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