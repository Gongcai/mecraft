#include "UILayout.h"

float UILayout::resolveX(const float uiWidth, const float controlWidth) const {
    float base = 0.0f;
    switch (anchor) {
    case Anchor::TopLeft:
    case Anchor::CenterLeft:
    case Anchor::BottomLeft: base = 0.0f; break;
    case Anchor::TopCenter:
    case Anchor::Center:
    case Anchor::BottomCenter: base = (uiWidth - controlWidth) * 0.5f; break;
    case Anchor::TopRight:
    case Anchor::CenterRight:
    case Anchor::BottomRight: base = uiWidth - controlWidth; break;
    }
    return base + offsetX;
}

float UILayout::resolveY(const float uiHeight, const float controlHeight) const {
    float base = 0.0f;
    switch (anchor) {
    case Anchor::BottomLeft:
    case Anchor::BottomCenter:
    case Anchor::BottomRight: base = 0.0f; break;
    case Anchor::CenterLeft:
    case Anchor::Center:
    case Anchor::CenterRight: base = (uiHeight - controlHeight) * 0.5f; break;
    case Anchor::TopLeft:
    case Anchor::TopCenter:
    case Anchor::TopRight: base = uiHeight - controlHeight; break;
    }
    return base + offsetY;
}
