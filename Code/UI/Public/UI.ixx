export module UI;

import Core;
import Core.glm;

export class UI final
{
public:

    UI() {}
    ~UI() {}
    UI(const UI&) = delete;

    void initialize();
    void update(double deltaSec);
    void render();

    bool isViewportGrabbed() const { return m_isViewportGrabbed; }
    bool isViewportFocused() const { return m_isViewportFocused; }
    bool hasViewportGainedFocused() const { return m_hasViewportGainedFocus; }
    glm::ivec2 getViewportPos() const { return m_viewportPos; }
    glm::ivec2 getViewportSize() const { return m_viewportSize; }

private:

    bool m_isViewportGrabbed = false;
    bool m_isViewportFocused = false;
    bool m_hasViewportGainedFocus = false;
    glm::ivec2 m_viewportPos = glm::ivec2(0);
    glm::ivec2 m_viewportSize = glm::ivec2(0);
};

export namespace Globals
{
    UI ui;
}