#pragma once

#include <cstdint>

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
    UICommand command = UICommand::None;
    std::uint32_t codepoint = 0;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
};

