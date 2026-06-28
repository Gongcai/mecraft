//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_WINDOW_H
#define MECRAFT_WINDOW_H


#include <string>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
class Window {
public:
    bool init(int width, int height, const char* title, bool enableGlDebugOutput);
    void destroy();

    [[nodiscard]] bool shouldClose() const;
    void swapBuffers() const;
    void pollEvents();

    [[nodiscard]] int getWidth() const;
    [[nodiscard]] int getHeight() const;
    [[nodiscard]] float getAspectRatio() const;
    void setTitle(const std::string& title) const;

    [[nodiscard]] GLFWwindow* getHandle() const;

private:
    GLFWwindow* m_window = nullptr;
    int m_width{}, m_height{};

    static void framebufferSizeCallback(GLFWwindow* w, int width, int height);
    static void APIENTRY debugMessageCallback(GLenum source,
                                              GLenum type,
                                              GLuint id,
                                              GLenum severity,
                                              GLsizei length,
                                              const GLchar* message,
                                              const void* userParam);
};


#endif //MECRAFT_WINDOW_H
