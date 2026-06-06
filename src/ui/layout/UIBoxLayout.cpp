#include "UIBoxLayout.h"

#include <algorithm>

void UIBoxLayout::setPadding(float all) {
    m_padding = {all, all, all, all};
}

void UIBoxLayout::setPadding(float horizontal, float vertical) {
    m_padding = {horizontal, vertical, horizontal, vertical};
}

void UIBoxLayout::setChildFlexGrow(const UIWidget* child, float grow) {
    if (!child || grow <= 0.0f) {
        m_flexGrow.erase(child);
        return;
    }
    m_flexGrow[child] = grow;
}

float UIBoxLayout::getChildFlexGrow(const UIWidget* child) const {
    const auto it = m_flexGrow.find(child);
    return it == m_flexGrow.end() ? 0.0f : it->second;
}

void UIBoxLayout::layout(const UIRenderContext& ctx) {
    layout();
    UIWidget::layout(ctx);
}

void UIBoxLayout::layout() {
    const auto& children = getChildren();
    const int visibleCount = static_cast<int>(std::count_if(children.begin(), children.end(),
        [](const auto& child) { return child && child->visible; }));
    if (visibleCount <= 0) {
        return;
    }

    const bool horizontal = m_direction == UIBoxDirection::Horizontal;
    const float innerMain = horizontal ? contentWidth() : contentHeight();
    const float innerCross = horizontal ? contentHeight() : contentWidth();
    const float totalGap = m_gap * static_cast<float>(std::max(0, visibleCount - 1));

    float fixedMain = 0.0f;
    float totalGrow = 0.0f;
    for (const auto& child : children) {
        if (!child || !child->visible) {
            continue;
        }
        const float grow = getChildFlexGrow(child.get());
        if (grow > 0.0f) {
            totalGrow += grow;
        } else {
            fixedMain += horizontal ? child->width : child->height;
        }
    }

    const float growSpace = std::max(0.0f, innerMain - fixedMain - totalGap);
    float usedMain = fixedMain + totalGap;
    for (const auto& child : children) {
        if (!child || !child->visible) {
            continue;
        }
        const float grow = getChildFlexGrow(child.get());
        if (grow <= 0.0f) {
            continue;
        }
        const float grownSize = totalGrow > 0.0f ? growSpace * (grow / totalGrow) : 0.0f;
        if (horizontal) {
            child->width = grownSize;
        } else {
            child->height = grownSize;
        }
        usedMain += grownSize;
    }

    const float remaining = std::max(0.0f, innerMain - usedMain);
    float gap = m_gap;
    float cursor = 0.0f;
    switch (m_justify) {
        case UIJustifyContent::Start:
            break;
        case UIJustifyContent::Center:
            cursor = remaining * 0.5f;
            break;
        case UIJustifyContent::End:
            cursor = remaining;
            break;
        case UIJustifyContent::SpaceBetween:
            gap = visibleCount > 1 ? m_gap + remaining / static_cast<float>(visibleCount - 1) : 0.0f;
            break;
    }

    for (const auto& child : children) {
        if (!child || !child->visible) {
            continue;
        }

        const float childMain = horizontal ? child->width : child->height;
        float childCross = horizontal ? child->height : child->width;
        float cross = 0.0f;

        switch (m_align) {
            case UIAlignItems::Start:
                break;
            case UIAlignItems::Center:
                cross = std::max(0.0f, (innerCross - childCross) * 0.5f);
                break;
            case UIAlignItems::End:
                cross = std::max(0.0f, innerCross - childCross);
                break;
            case UIAlignItems::Stretch:
                childCross = innerCross;
                if (horizontal) {
                    child->height = childCross;
                } else {
                    child->width = childCross;
                }
                break;
        }

        child->anchor = Anchor::BottomLeft;
        child->anchorOffsetX = 0.0f;
        child->anchorOffsetY = 0.0f;
        child->x = horizontal ? m_padding.left + cursor : m_padding.left + cross;
        child->y = horizontal ? m_padding.bottom + cross : m_padding.bottom + innerMain - cursor - childMain;
        cursor += childMain + gap;
    }
}

float UIBoxLayout::contentWidth() const {
    return std::max(0.0f, width - m_padding.left - m_padding.right);
}

float UIBoxLayout::contentHeight() const {
    return std::max(0.0f, height - m_padding.top - m_padding.bottom);
}
