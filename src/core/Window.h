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
    bool init(int width, int height, const char* title);
    void destroy();

    [[nodiscard]] bool shouldClose() const;
     void swapBuffers() const;
    void pollEvents();       // 仅驱动 GLFW 事件泵, 由 InputManager 消费

    [[nodiscard]] int getWidth() const;
    [[nodiscard]] int getHeight() const;
    [[nodiscard]] float getAspectRatio() const;
    void setTitle(const std::string& title) const;

    // 供 InputManager 注册回调时使用
    [[nodiscard]] GLFWwindow* getHandle() const;

private:
    GLFWwindow* m_window = nullptr;
    int m_width{}, m_height{};

    // 窗口级回调 (仅处理窗口本身的事件)
    static void framebufferSizeCallback(GLFWwindow* w, int width, int height);
};


#endif //MECRAFT_WINDOW_H