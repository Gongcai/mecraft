#include "UILayout.h"

float UILayout::resolveX(const float screenW, const float controlW) const {
    float base = 0.0f;
    switch (anchor) {
    case Anchor::TopLeft:
    case Anchor::CenterLeft:
    case Anchor::BottomLeft: base = 0.0f; break;
    case Anchor::TopCenter:
    case Anchor::Center:
    case Anchor::BottomCenter: base = (screenW - controlW) * 0.5f; break;
    case Anchor::TopRight:
    case Anchor::CenterRight:
    case Anchor::BottomRight: base = screenW - controlW; break;
    }
    return base + offsetX;
}

float UILayout::resolveY(const float screenH, const float controlH) const {
    float base = 0.0f;
    switch (anchor) {
    case Anchor::BottomLeft:
    case Anchor::BottomCenter:
    case Anchor::BottomRight: base = 0.0f; break;
    case Anchor::CenterLeft:
    case Anchor::Center:
    case Anchor::CenterRight: base = (screenH - controlH) * 0.5f; break;
    case Anchor::TopLeft:
    case Anchor::TopCenter:
    case Anchor::TopRight: base = screenH - controlH; break;
    }
    return base + offsetY;
}
