#include "Window.h"

bool Window::init(int width, int height, const char *title) {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
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
    m_height = height;
    m_width = width;

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
    return m_width;
}

int Window::getHeight() const {
    return m_height;
}

float Window::getAspectRatio() const {
    return static_cast<float>(m_width) / m_height;
}

void Window::setTitle(const std::string &title) const {
    glfwSetWindowTitle(m_window, title.c_str());
}

GLFWwindow * Window::getHandle() const {
    return m_window;
}

void Window::framebufferSizeCallback(GLFWwindow *w, int width, int height) {
    glViewport(0, 0, width, height);
}

