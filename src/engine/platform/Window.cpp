#include "Window.h"

#include <iostream>
#include <stdexcept>

bool Window::init(int width, int height, const char *title) {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    m_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (m_window == NULL) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(m_window);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        return false;
    }
    const auto* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* glslVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    std::cout << "OpenGL: " << (glVersion != nullptr ? glVersion : "unknown") << "\n";
    std::cout << "GLSL: " << (glslVersion != nullptr ? glslVersion : "unknown") << "\n";
    std::cout << "GLAD OpenGL 4.5: " << (GLAD_GL_VERSION_4_5 ? "yes" : "no") << "\n";
    if (!GLAD_GL_VERSION_4_5) {
        throw std::runtime_error("OpenGL 4.5 core is required for the hybrid deferred renderer.");
    }
#ifdef MECRAFT_DEBUG
    if (GLAD_GL_VERSION_4_3) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(Window::debugMessageCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_PERFORMANCE, GL_DONT_CARE, 0, nullptr, GL_FALSE);
    }
#endif
    int framebufferWidth = width;
    int framebufferHeight = height;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    m_width = framebufferWidth > 0 ? framebufferWidth : width;
    m_height = framebufferHeight > 0 ? framebufferHeight : height;
    glfwSwapInterval(0);
    return true;
}

void Window::destroy() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    m_width =0;
    m_height =0;
    glfwTerminate();
}



bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::swapBuffers() const {
    glfwSwapBuffers(m_window);
}

void Window::pollEvents() {
    glfwPollEvents();
}

int Window::getWidth() const {
    if (m_window == nullptr) {
        return m_width;
    }
    int framebufferWidth = m_width;
    int framebufferHeight = m_height;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    return framebufferWidth > 0 ? framebufferWidth : m_width;
}

int Window::getHeight() const {
    if (m_window == nullptr) {
        return m_height;
    }
    int framebufferWidth = m_width;
    int framebufferHeight = m_height;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    return framebufferHeight > 0 ? framebufferHeight : m_height;
}

float Window::getAspectRatio() const {
    const int width = getWidth();
    const int height = getHeight();
    return static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
}

void Window::setTitle(const std::string &title) const {
    glfwSetWindowTitle(m_window, title.c_str());
}

GLFWwindow * Window::getHandle() const {
    return m_window;
}

void Window::framebufferSizeCallback(GLFWwindow *w, int width, int height) {
    (void) w;
    glViewport(0, 0, width, height);
}

void APIENTRY Window::debugMessageCallback(GLenum source,
                                           GLenum type,
                                           GLuint id,
                                           GLenum severity,
                                           GLsizei length,
                                           const GLchar* message,
                                           const void* userParam) {
    (void)source;
    (void)id;
    (void)length;
    (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || type == GL_DEBUG_TYPE_PERFORMANCE) {
        return;
    }
    std::cerr << "OpenGL debug: " << (message != nullptr ? message : "") << "\n";
}
