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
#include <array>
#include <cstddef>
#include <cstdint>

struct InputSnapshot {
    static constexpr size_t kMaxTypedCharsPerFrame = 64;
    static constexpr double kDefaultDoubleTapTimeout = 0.3; // 双击最大间隔（秒）

    bool keys[GLFW_KEY_LAST + 1] = {};
    bool keysJustPressed[GLFW_KEY_LAST + 1] = {};
    bool keysJustReleased[GLFW_KEY_LAST + 1] = {};
    bool keysDoubleTapped[GLFW_KEY_LAST + 1] = {};  // 本帧检测到键盘双击

    bool mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool mouseButtonsJustPressed[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool mouseButtonsJustReleased[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool mouseButtonsDoubleTapped[GLFW_MOUSE_BUTTON_LAST + 1] = {};  // 本帧检测到鼠标双击

    glm::vec2 mousePosition{0.0f, 0.0f};
    glm::vec2 mouseDelta{0.0f, 0.0f};
    double scrollDelta = 0.0;  // >0 = scroll up, <0 = scroll down
    std::array<uint32_t, kMaxTypedCharsPerFrame> typedChars{};
    size_t typedCharCount = 0;

    struct UIDragPayload {
        bool active = false;
        int itemId = 0;
        int count = 0;
        int sourceSlot = -1;
        glm::vec2 pointerPosition{0.0f, 0.0f};
    };
    UIDragPayload draggedItem{};

    // Gamepad state (using GLFW gamepad mapping)
    struct GamepadState {
        bool connected = false;

        // Button states (15 buttons in GLFW_GAMEPAD standard)
        bool buttons[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
        bool buttonsJustPressed[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
        bool buttonsJustReleased[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
        bool buttonsDoubleTapped[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};

        // Analog axes (6 axes: LStickX/Y, RStickX/Y, LT, RT)
        float axes[GLFW_GAMEPAD_AXIS_LAST + 1] = {};

        // Dead zone configuration
        static constexpr float kStickDeadZone = 0.15f;
        static constexpr float kTriggerThreshold = 0.1f;
    };
    GamepadState gamepad{};

    [[nodiscard]] bool isKeyHeld(int key) const;
    [[nodiscard]] bool isKeyJustPressed(int key) const;
    [[nodiscard]] bool isKeyJustReleased(int key) const;
    [[nodiscard]] bool isKeyDoubleTapped(int key) const;
    [[nodiscard]] bool isMouseButtonHeld(int button) const;
    [[nodiscard]] bool isMouseButtonJustPressed(int button) const;
    [[nodiscard]] bool isMouseButtonJustReleased(int button) const;
    [[nodiscard]] bool isMouseButtonDoubleTapped(int button) const;

    // Gamepad query methods
    [[nodiscard]] bool isGamepadConnected() const;
    [[nodiscard]] bool isGamepadButtonHeld(int button) const;
    [[nodiscard]] bool isGamepadButtonJustPressed(int button) const;
    [[nodiscard]] bool isGamepadButtonJustReleased(int button) const;
    [[nodiscard]] bool isGamepadButtonDoubleTapped(int button) const;
    [[nodiscard]] float getGamepadAxis(int axis) const;
};

class InputManager {
public:
#ifdef MECRAFT_DEBUG
    struct DebugEventStats {
        uint32_t keyEvents = 0;
        uint32_t mouseButtonEvents = 0;
        uint32_t cursorPosEvents = 0;
        uint32_t scrollEvents = 0;
        uint32_t charEvents = 0;
        double keyCallbackMs = 0.0;
        double mouseButtonCallbackMs = 0.0;
        double cursorPosCallbackMs = 0.0;
        double scrollCallbackMs = 0.0;
        double charCallbackMs = 0.0;

        [[nodiscard]] uint32_t total() const {
            return keyEvents + mouseButtonEvents + cursorPosEvents + scrollEvents + charEvents;
        }

        [[nodiscard]] double callbackMs() const {
            return keyCallbackMs + mouseButtonCallbackMs + cursorPosCallbackMs + scrollCallbackMs + charCallbackMs;
        }
    };
#endif

    // 传入 Window 句柄以注册 GLFW 回调
    void init(GLFWwindow* windowHandle);

    // 每帧调用：刷新状态并生成只读快照
    void update();
    [[nodiscard]] const InputSnapshot& snapshot() const;

    // ── 鼠标模式 ──
    void captureMouse(bool capture);       // true → GLFW_CURSOR_DISABLED
    void resetMouseDelta();              // 重置鼠标位移

    // Shared UI drag payload channel for inventory/other UI systems.
    void beginUIDragItem(int itemId, int count, int sourceSlot);
    void clearUIDragItem();
    [[nodiscard]] const InputSnapshot::UIDragPayload& getUIDragItem() const;

#ifdef MECRAFT_DEBUG
    void resetDebugEventStats() { m_debugEventStats = {}; }
    [[nodiscard]] const DebugEventStats& debugEventStats() const { return m_debugEventStats; }
#endif
private:
    GLFWwindow* m_handle = nullptr;

    // 键盘状态双缓冲
    bool m_keys[GLFW_KEY_LAST + 1] = {};
    bool m_keysPrev[GLFW_KEY_LAST + 1] = {};
    bool m_keysJustPressed[GLFW_KEY_LAST + 1] = {};
    bool m_keysJustReleased[GLFW_KEY_LAST + 1] = {};

    // 键盘双击追踪
    double m_keyLastPressTime[GLFW_KEY_LAST + 1] = {}; // 上次按下时的时间戳
    double m_doubleTapTimeout = InputSnapshot::kDefaultDoubleTapTimeout;

    // 鼠标按键双缓冲
    bool m_mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool m_mouseButtonsPrev[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool m_mouseButtonsJustPressed[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool m_mouseButtonsJustReleased[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    // 鼠标双击追踪
    double m_mouseButtonLastPressTime[GLFW_MOUSE_BUTTON_LAST + 1] = {};

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
    std::array<uint32_t, InputSnapshot::kMaxTypedCharsPerFrame> m_typedChars{};
    size_t m_typedCharCount = 0;

    InputSnapshot m_snapshot{};
    InputSnapshot::UIDragPayload m_draggedItem{};

    // Gamepad state tracking (joystick 0 = first gamepad)
    static constexpr int kGamepadJoystickId = GLFW_JOYSTICK_1;
    bool m_gamepadButtons[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
    bool m_gamepadButtonsPrev[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
    bool m_gamepadButtonsJustPressed[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
    bool m_gamepadButtonsJustReleased[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
    double m_gamepadButtonLastPressTime[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
    float m_gamepadAxes[GLFW_GAMEPAD_AXIS_LAST + 1] = {};
    bool m_gamepadConnected = false;

#ifdef MECRAFT_DEBUG
    DebugEventStats m_debugEventStats{};
#endif

    static InputManager* fromWindow(GLFWwindow* w);

    // Helper for applying dead zone to stick axes
    static float applyDeadZone(float value, float deadZone);

    // GLFW 回调 (static → 通过 userPointer 转发到实例)
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* w, double xoffset, double yoffset);
    static void charCallback(GLFWwindow* w, unsigned int codepoint);
};


#endif //MECRAFT_INPUTMANAGER_H

