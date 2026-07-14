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
    return true;
}

void Window::destroy() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    m_width =0;
    m_height =0;
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
    if (framebufferWidth > 0 && framebufferHeight > 0) {
        return {framebufferWidth, framebufferHeight};
    }
    return {m_width, m_height};
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

GLFWwindow * Window::getHandle() const {
    return m_window;
}
