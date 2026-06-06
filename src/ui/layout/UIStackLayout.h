#pragma once

#include <algorithm>

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

    void layout(const UIRenderContext& ctx) override {
        layout();
        UIWidget::layout(ctx);
    }

    // Recalculate child positions based on direction and spacing
    void layout() {
        if (m_direction == StackDirection::Vertical) {
            float totalHeight = 0.0f;
            float maxW = 0.0f;
            for (auto& child : getChildren()) {
                totalHeight += child->height;
                if (totalHeight > child->height) {
                    totalHeight += m_spacing;
                }
                if (child->width > maxW) maxW = child->width;
            }
            height = totalHeight;
            width = maxW;

            float offset = 0.0f;
            for (auto& child : getChildren()) {
                child->anchor = Anchor::BottomLeft;
                child->x = 0.0f;
                child->y = height - offset - child->height;
                offset += child->height + m_spacing;
            }
        } else {
            float totalWidth = 0.0f;
            float maxH = 0.0f;
            for (auto& child : getChildren()) {
                totalWidth += child->width;
                if (totalWidth > child->width) {
                    totalWidth += m_spacing;
                }
                if (child->height > maxH) maxH = child->height;
            }
            width = totalWidth;
            height = maxH;

            float offset = 0.0f;
            for (auto& child : getChildren()) {
                child->anchor = Anchor::BottomLeft;
                child->x = offset;
                child->y = 0.0f;
                offset += child->width + m_spacing;
            }
        }
    }

private:
    StackDirection m_direction = StackDirection::Vertical;
    float m_spacing = 8.0f;
};
