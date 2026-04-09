//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_INPUTMANAGER_H
#define MECRAFT_INPUTMANAGER_H


#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

struct InputSnapshot {
    bool keys[GLFW_KEY_LAST + 1] = {};
    bool keysJustPressed[GLFW_KEY_LAST + 1] = {};
    bool keysJustReleased[GLFW_KEY_LAST + 1] = {};

    bool mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool mouseButtonsJustPressed[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool mouseButtonsJustReleased[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    glm::vec2 mousePosition{0.0f, 0.0f};
    glm::vec2 mouseDelta{0.0f, 0.0f};
    double scrollDelta = 0.0;  // >0 = scroll up, <0 = scroll down

    [[nodiscard]] bool isKeyHeld(int key) const;
    [[nodiscard]] bool isKeyJustPressed(int key) const;
    [[nodiscard]] bool isKeyJustReleased(int key) const;
    [[nodiscard]] bool isMouseButtonHeld(int button) const;
    [[nodiscard]] bool isMouseButtonJustPressed(int button) const;
    [[nodiscard]] bool isMouseButtonJustReleased(int button) const;
};

class InputManager {
public:
    // 传入 Window 句柄以注册 GLFW 回调
    void init(GLFWwindow* windowHandle);

    // 每帧调用：刷新状态并生成只读快照
    void update();
    [[nodiscard]] const InputSnapshot& snapshot() const;

    // ── 鼠标模式 ──
    void captureMouse(bool capture);       // true → GLFW_CURSOR_DISABLED
    void resetMouseDelta();              // 重置鼠标位移
private:
    GLFWwindow* m_handle = nullptr;

    // 键盘状态双缓冲
    bool m_keys[GLFW_KEY_LAST + 1] = {};
    bool m_keysPrev[GLFW_KEY_LAST + 1] = {};
    bool m_keysJustPressed[GLFW_KEY_LAST + 1] = {};
    bool m_keysJustReleased[GLFW_KEY_LAST + 1] = {};

    // 鼠标按键双缓冲
    bool m_mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool m_mouseButtonsPrev[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool m_mouseButtonsJustPressed[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool m_mouseButtonsJustReleased[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    // 鼠标位置
    double m_mouseX = 0.0;
    double m_mouseY = 0.0;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    double m_mouseDeltaX = 0.0;
    double m_mouseDeltaY = 0.0;
    double m_accumDeltaX = 0.0;
    double m_accumDeltaY = 0.0;
    double m_accumScrollY = 0.0;
    bool m_firstMouse = true;

    InputSnapshot m_snapshot{};

    static InputManager* fromWindow(GLFWwindow* w);

    // GLFW 回调 (static → 通过 userPointer 转发到实例)
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* w, double xoffset, double yoffset);
};


#endif //MECRAFT_INPUTMANAGER_H

