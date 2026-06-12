#pragma once

#include <cstdint>

enum class UIInputModifier {
    Shift   = 1 << 0,
    Control = 1 << 1,
    Alt     = 1 << 2,
    Super   = 1 << 3,
};

[[nodiscard]] inline constexpr int uiInputModifierMask(UIInputModifier modifier) {
    return static_cast<int>(modifier);
}

[[nodiscard]] inline constexpr bool hasInputModifier(int modifiers, UIInputModifier modifier) {
    return (modifiers & uiInputModifierMask(modifier)) != 0;
}

enum class UIInputEventType {
    PointerMove,
    PointerDown,
    PointerUp,
    KeyDown,
    KeyUp,
    Command,
    TextInput,
    Scroll,
};

enum class UICommand {
    None,
    NavigateUp,
    NavigateDown,
    NavigateLeft,
    NavigateRight,
    Activate,
    Cancel,
    Home,
    End,
    TabLeft,
    TabRight,
};

enum class UIPointerButton {
    None = -1,
    Primary = 0,
    Secondary = 1,
};

struct UIInputEvent {
    UIInputEventType type = UIInputEventType::PointerMove;
    float x = 0.0f;
    float y = 0.0f;
    UIPointerButton button = UIPointerButton::None;
    int key = 0;
    int modifiers = 0;
    UICommand command = UICommand::None;
    std::uint32_t codepoint = 0;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
};

