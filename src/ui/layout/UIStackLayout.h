#pragma once

#include "../core/UIWidget.h"

enum class StackDirection {
    Vertical,
    Horizontal,
};

class UIStackLayout : public UIWidget {
public:
    UIStackLayout() = default;

    void setDirection(StackDirection dir) { m_direction = dir; }
    [[nodiscard]] StackDirection getDirection() const { return m_direction; }

    void setSpacing(float spacing) { m_spacing = spacing; }
    [[nodiscard]] float getSpacing() const { return m_spacing; }

    // Recalculate child positions based on direction and spacing
    void layout() {
        float offset = 0.0f;
        for (auto& child : getChildren()) {
            if (m_direction == StackDirection::Vertical) {
                child->y = offset;
                child->x = 0.0f;
                offset += child->height + m_spacing;
            } else {
                child->x = offset;
                child->y = 0.0f;
                offset += child->width + m_spacing;
            }
        }
        // Update own size to fit children
        if (m_direction == StackDirection::Vertical) {
            height = offset - m_spacing;
            float maxW = 0.0f;
            for (auto& child : getChildren()) {
                if (child->width > maxW) maxW = child->width;
            }
            width = maxW;
        } else {
            width = offset - m_spacing;
            float maxH = 0.0f;
            for (auto& child : getChildren()) {
                if (child->height > maxH) maxH = child->height;
            }
            height = maxH;
        }
    }

private:
    StackDirection m_direction = StackDirection::Vertical;
    float m_spacing = 8.0f;
};
