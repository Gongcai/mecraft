//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_WINDOW_H
#define MECRAFT_WINDOW_H


#include <string>

struct GLFWwindow;

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
};


#endif //MECRAFT_WINDOW_H
