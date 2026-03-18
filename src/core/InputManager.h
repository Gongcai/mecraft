//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_INPUTMANAGER_H
#define MECRAFT_INPUTMANAGER_H


#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

class InputManager {
public:
    // 传入 Window 句柄以注册 GLFW 回调
    void init(GLFWwindow* windowHandle);

    // 每逻辑帧开始前调用：将当前帧状态备份为"上一帧"
    void update();

    // ── 键盘 ──
    [[nodiscard]] bool isKeyHeld(int key) const;         // 持续按住
    [[nodiscard]] bool isKeyJustPressed(int key) const;  // 本帧刚按下
    [[nodiscard]] bool isKeyJustReleased(int key) const; // 本帧刚释放

    // ── 鼠标按键 ──
    [[nodiscard]] bool isMouseButtonHeld(int button) const;
    [[nodiscard]] bool isMouseButtonJustPressed(int button) const;

    // ── 鼠标移动 ──
    [[nodiscard]] glm::vec2 getMousePosition() const;
    [[nodiscard]] glm::vec2 getMouseDelta() const;       // 本帧相对位移

    // ── 鼠标模式 ──
    void captureMouse(bool capture);       // true → GLFW_CURSOR_DISABLED
    void resetMouseDelta();              // 重置鼠标位移
private:
    GLFWwindow* m_handle = nullptr;

    // 键盘状态双缓冲
    bool m_keys[GLFW_KEY_LAST + 1]     = {};
    bool m_keysPrev[GLFW_KEY_LAST + 1] = {};
    bool m_keysJustPressed[GLFW_KEY_LAST + 1]  = {};
    bool m_keysJustReleased[GLFW_KEY_LAST + 1] = {};

    // 鼠标按键双缓冲
    bool m_mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1]     = {};
    bool m_mouseButtonsPrev[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool m_mouseButtonsJustPressed[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    // 鼠标位置
    double m_mouseX = 0, m_mouseY = 0;
    double m_lastMouseX = 0, m_lastMouseY = 0;
    double m_mouseDeltaX = 0, m_mouseDeltaY = 0;
    double m_accumDeltaX = 0, m_accumDeltaY = 0; // Cumulative delta from callbacks
    bool   m_firstMouse = true;

    // GLFW 回调 (static → 通过 userPointer 转发到实例)
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* w, double xoffset, double yoffset);
};


#endif //MECRAFT_INPUTMANAGER_H