export module Core.Window;

import Core;

export class Window final
{
public:
	Window();
	~Window();
	Window(const Window&) = delete;

	bool initialize(std::string_view windowTitle, glm::ivec2 pos, glm::ivec2 size);

	void* getWindowHandle() const { return m_windowHandle; }
	void setTitle(std::string_view title);
	void getWindowSize(glm::ivec2& size) const;

private:
	void* m_windowHandle = nullptr;
};