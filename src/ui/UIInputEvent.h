#pragma once

enum class UIInputEventType {
    PointerMove,
    PointerDown,
    PointerUp,
};

struct UIInputEvent {
    UIInputEventType type = UIInputEventType::PointerMove;
    float x = 0.0f;
    float y = 0.0f;
    int button = 0;
};

