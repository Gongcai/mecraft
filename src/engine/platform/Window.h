//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_WINDOW_H
#define MECRAFT_WINDOW_H


#include <string>

struct GLFWwindow;

class Window {
public:
    struct FramebufferSize {
        int width = 0;
        int height = 0;
    };

    bool initializePlatform();
    bool create(int width, int height, const char* title);
    void destroy();

    [[nodiscard]] bool shouldClose() const;
    void pollEvents();

    [[nodiscard]] FramebufferSize getFramebufferSize() const;
    [[nodiscard]] int getWidth() const;
    [[nodiscard]] int getHeight() const;
    [[nodiscard]] float getAspectRatio() const;
    void setTitle(const std::string& title) const;

    /// Changes the GLFW window between its saved windowed placement and primary-monitor mode.
    /// @param enabled True requests fullscreen mode; false restores the saved windowed placement.
    /// @return True when GLFW reports the requested monitor attachment state.
    [[nodiscard]] bool setFullscreen(bool enabled);

    /// Reports whether the window is currently attached to a monitor.
    /// @return True while the GLFW window is in fullscreen mode.
    [[nodiscard]] bool isFullscreen() const;

    /// Reports whether the current GLFW platform can perform a fullscreen state change.
    /// @return True when a live window and a usable monitor mode are available.
    [[nodiscard]] bool fullscreenControlAvailable() const;

    [[nodiscard]] GLFWwindow* getHandle() const;

private:
    GLFWwindow* m_window = nullptr;
    bool m_platformInitialized = false;
    int m_width{}, m_height{};
    int m_windowedX = 0;
    int m_windowedY = 0;
    int m_windowedWidth = 0;
    int m_windowedHeight = 0;
    bool m_fullscreen = false;
};


#endif //MECRAFT_WINDOW_H
