//
// Created by Caiwe on 2026/3/22.
//

#ifndef MECRAFT_ACTIONMAP_H
#define MECRAFT_ACTIONMAP_H

#include <vector>
#include <unordered_map>
#include <string>
#include "engine/input/InputManager.h"

enum class InputContextType {
    Gameplay,
    UI,
    Pause,
    // Add others if needed
};

enum class Axis {
    Vertical,   // 控制前后
    Horizontal, // 控制左右
    LookX,      // X轴视角移动
    LookY       // Y轴视角移动
};

enum class Action {
    Jump,
    Sprint,
    Crouch,
    // Mouse actions
    Attack,    // e.g. Left Click
    UseItem,   // e.g. Right Click
    Inventory, // e.g. E or I
    Menu,      // e.g. ESC
    // Hotbar slot selection
    Hotbar1,
    Hotbar2,
    Hotbar3,
    Hotbar4,
    Hotbar5,
    Hotbar6,
    Hotbar7,
    Hotbar8,
    Hotbar9,
    HotbarScrollUp,
    HotbarScrollDown,
    // UI Actions
    Confirm,
    Cancel,
    UIPrimaryClick,
    UISecondaryClick,
    UIPrimaryRelease,
    UISecondaryRelease,
    Up,
    Down,
    Left,
    Right,
    Backspace,
    OpenCommand,
    ToggleViewMode
};

enum class InputDevice {
    Keyboard,
    Mouse,
    Gamepad,
    Scroll
};

enum class TriggerType {
    Pressed,
    Released,
    Held,
    DoubleTap,
    // Future: Tap, Hold
};

enum class NativeAxis {
    None,
    MouseX,
    MouseY,
    // Gamepad axes
    GamepadLeftStickX,
    GamepadLeftStickY,
    GamepadRightStickX,
    GamepadRightStickY,
    GamepadLeftTrigger,
    GamepadRightTrigger
};

struct InputBinding {
    InputContextType context = InputContextType::Gameplay;
    InputDevice device = InputDevice::Keyboard;
    int control = 0; // GLFW_KEY_* or GLFW_MOUSE_BUTTON_* or GLFW_GAMEPAD_BUTTON_*
    TriggerType trigger = TriggerType::Held;
    int modifiers = 0; // GLFW_MOD_* bitmask (Shift=1, Ctrl=2, Alt=4, Super=8)
};

struct AxisBinding {
    InputContextType context = InputContextType::Gameplay;
    int positiveKey = 0; // 例: GLFW_KEY_W (加1)
    int negativeKey = 0; // 例: GLFW_KEY_S (减1)
    NativeAxis nativeAxis = NativeAxis::None;
    bool invert = false;
};

class ActionMap {
public:
    // Load bindings from a configuration file
    void loadFromFile(const std::string& path);

    // Bind a keyboard key to an action with specific trigger type
    void bindKey(Action action, int keyCode, TriggerType trigger, InputContextType context = InputContextType::Gameplay);

    // Bind a keyboard key to an action
    void bindKey(Action action, int keyCode, InputContextType context = InputContextType::Gameplay);

    // Bind Axis to key
    void bindAxisKey(Axis axis, int positiveKeyCode, int negativeKeyCode, InputContextType context = InputContextType::Gameplay, bool invert = false);

    // Bind Native Axis
    void bindNativeAxis(Axis axis, NativeAxis native, InputContextType context = InputContextType::Gameplay, bool invert = false);

    // Bind a mouse button to an action
    void bindMouseButton(Action action, int buttonCode, InputContextType context = InputContextType::Gameplay);

    // Bind a gamepad button to an action
    void bindGamepadButton(Action action, int buttonCode, TriggerType trigger = TriggerType::Pressed, InputContextType context = InputContextType::Gameplay);

    // Clear all bindings
    void clearAll();

    // Check if the action is currently triggered in a specific context
    // This is the low-level check
    [[nodiscard]] bool isActionTriggered(Action action, InputContextType context, const InputSnapshot& input) const;
    [[nodiscard]] bool isActionDoubleTapped(Action action, InputContextType context, const InputSnapshot& input) const;
    [[nodiscard]] float getAxisValue(Axis axis,InputContextType context, const InputSnapshot& input) const;

private:
   // Store bindings per Action
   // When checking, we iterate bindings for the Action and check if context matches and input matches.
    std::unordered_map<Action, std::vector<InputBinding>> m_bindings;
    std::unordered_map<Axis, std::vector<AxisBinding>> m_axisBindings;

    // Helper to evaluate a single binding
    bool evaluateBinding(const InputBinding& binding, const InputSnapshot& input) const;
};

#endif //MECRAFT_ACTIONMAP_H