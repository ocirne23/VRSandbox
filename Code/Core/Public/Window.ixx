export module Core.Window;

import Core;
import Core.glm;

export import Core.fwd;

export class Window
{
public:

    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window(const Window&&) = delete;
    Window& operator=(const Window&) = delete;
    Window& operator=(const Window&&) = delete;

    bool initialize(std::string_view windowTitle, glm::ivec2 pos, glm::ivec2 size);

    void* getWindowHandle() const { return m_windowHandle; }
    void setTitle(std::string_view title);
    void getWindowSize(glm::ivec2& size) const;

private:
    void* m_windowHandle = nullptr;
};