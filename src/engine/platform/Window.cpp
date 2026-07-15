#include "Window.h"

#include "../../Diagnostics.h"

#include <GLFW/glfw3.h>
#include <iostream>

namespace {

void glfwErrorCallback(int error, const char* description) {
    MECRAFT_LOG_STREAM(std::cerr << "GLFW error " << error << ": "
                                 << (description != nullptr ? description : "unknown") << "\n");
}

} // namespace

bool Window::initializePlatform() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        const char* description = nullptr;
        const int error = glfwGetError(&description);
        MECRAFT_LOG_STREAM(std::cerr << "Failed to initialize GLFW");
        if (error != GLFW_NO_ERROR) {
            MECRAFT_LOG_STREAM(std::cerr << " (" << error << ": "
                                         << (description != nullptr ? description : "unknown") << ")");
        }
        MECRAFT_LOG_STREAM(std::cerr << "\n");
        return false;
    }
    m_platformInitialized = true;
    return true;
}

bool Window::create(const int width, const int height, const char* title) {
    if (!m_platformInitialized || m_window != nullptr) {
        MECRAFT_LOG_STREAM(std::cerr << "Window creation requires an initialized platform and no existing window\n");
        return false;
    }
    m_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (m_window == NULL) {
        const char* description = nullptr;
        const int error = glfwGetError(&description);
        MECRAFT_LOG_STREAM(std::cerr << "Failed to create GLFW window");
        if (error != GLFW_NO_ERROR) {
            MECRAFT_LOG_STREAM(std::cerr << " (" << error << ": "
                                         << (description != nullptr ? description : "unknown") << ")");
        }
        MECRAFT_LOG_STREAM(std::cerr << "\n");
        return false;
    }
    int framebufferWidth = width;
    int framebufferHeight = height;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    m_width = framebufferWidth > 0 ? framebufferWidth : width;
    m_height = framebufferHeight > 0 ? framebufferHeight : height;
    glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
    glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);
    m_fullscreen = false;
    return true;
}

void Window::destroy() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    m_width =0;
    m_height =0;
    m_windowedX = 0;
    m_windowedY = 0;
    m_windowedWidth = 0;
    m_windowedHeight = 0;
    m_fullscreen = false;
    if (m_platformInitialized) {
        glfwTerminate();
        m_platformInitialized = false;
    }
}



bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents() {
    glfwPollEvents();
}

Window::FramebufferSize Window::getFramebufferSize() const {
    if (m_window == nullptr) {
        return {m_width, m_height};
    }
    int framebufferWidth = m_width;
    int framebufferHeight = m_height;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    return {framebufferWidth, framebufferHeight};
}

int Window::getWidth() const {
    return getFramebufferSize().width;
}

int Window::getHeight() const {
    return getFramebufferSize().height;
}

float Window::getAspectRatio() const {
    const FramebufferSize size = getFramebufferSize();
    return static_cast<float>(size.width) /
           static_cast<float>(size.height > 0 ? size.height : 1);
}

void Window::setTitle(const std::string &title) const {
    glfwSetWindowTitle(m_window, title.c_str());
}

bool Window::setFullscreen(const bool enabled) {
    if (m_window == nullptr) {
        return false;
    }
    if (enabled == m_fullscreen) {
        return true;
    }

    (void)glfwGetError(nullptr);
    if (enabled) {
        GLFWmonitor* const monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* const mode =
            monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor == nullptr || mode == nullptr) {
            return false;
        }
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);
        if (m_windowedWidth <= 0 || m_windowedHeight <= 0) {
            return false;
        }
        glfwSetWindowMonitor(m_window,
                             monitor,
                             0,
                             0,
                             mode->width,
                             mode->height,
                             mode->refreshRate);
    } else {
        if (m_windowedWidth <= 0 || m_windowedHeight <= 0) {
            return false;
        }
        glfwSetWindowMonitor(m_window,
                             nullptr,
                             m_windowedX,
                             m_windowedY,
                             m_windowedWidth,
                             m_windowedHeight,
                             GLFW_DONT_CARE);
    }
    const int error = glfwGetError(nullptr);
    m_fullscreen = glfwGetWindowMonitor(m_window) != nullptr;
    return error == GLFW_NO_ERROR && m_fullscreen == enabled;
}

bool Window::isFullscreen() const {
    return m_window != nullptr && m_fullscreen;
}

bool Window::fullscreenControlAvailable() const {
    if (m_window == nullptr) {
        return false;
    }
    if (m_fullscreen) {
        return true;
    }
    GLFWmonitor* const monitor = glfwGetPrimaryMonitor();
    return monitor != nullptr && glfwGetVideoMode(monitor) != nullptr;
}

GLFWwindow * Window::getHandle() const {
    return m_window;
}
