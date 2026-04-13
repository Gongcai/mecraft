#pragma once

enum class UIInputEventType {
    PointerMove,
    PointerDown,
    PointerUp,
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
};

