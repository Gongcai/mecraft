#pragma once

#include <cstdint>

enum class UIInputEventType {
    PointerMove,
    PointerDown,
    PointerUp,
    KeyDown,
    KeyUp,
    TextInput,
    Scroll,
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
    std::uint32_t codepoint = 0;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
};

