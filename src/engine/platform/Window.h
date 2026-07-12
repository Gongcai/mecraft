//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_WINDOW_H
#define MECRAFT_WINDOW_H


#include <string>

struct GLFWwindow;

class Window {
public:
    bool initializePlatform();
    bool create(int width, int height, const char* title);
    void destroy();

    [[nodiscard]] bool shouldClose() const;
    void pollEvents();

    [[nodiscard]] int getWidth() const;
    [[nodiscard]] int getHeight() const;
    [[nodiscard]] float getAspectRatio() const;
    void setTitle(const std::string& title) const;

    [[nodiscard]] GLFWwindow* getHandle() const;

private:
    GLFWwindow* m_window = nullptr;
    bool m_platformInitialized = false;
    int m_width{}, m_height{};
};


#endif //MECRAFT_WINDOW_H
