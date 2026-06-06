#pragma once

#include <unordered_map>

#include "../core/UIWidget.h"

struct UIEdgeInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class UIBoxDirection {
    Vertical,
    Horizontal,
};

enum class UIJustifyContent {
    Start,
    Center,
    End,
    SpaceBetween,
};

enum class UIAlignItems {
    Start,
    Center,
    End,
    Stretch,
};

class UIBoxLayout : public UIWidget {
public:
    UIBoxLayout() = default;

    void setDirection(UIBoxDirection direction) { m_direction = direction; }
    [[nodiscard]] UIBoxDirection getDirection() const { return m_direction; }

    void setGap(float gap) { m_gap = gap; }
    [[nodiscard]] float getGap() const { return m_gap; }

    void setPadding(float all);
    void setPadding(float horizontal, float vertical);
    void setPadding(const UIEdgeInsets& padding) { m_padding = padding; }
    [[nodiscard]] const UIEdgeInsets& getPadding() const { return m_padding; }

    void setJustifyContent(UIJustifyContent justify) { m_justify = justify; }
    [[nodiscard]] UIJustifyContent getJustifyContent() const { return m_justify; }

    void setAlignItems(UIAlignItems align) { m_align = align; }
    [[nodiscard]] UIAlignItems getAlignItems() const { return m_align; }

    void setChildFlexGrow(const UIWidget* child, float grow);
    [[nodiscard]] float getChildFlexGrow(const UIWidget* child) const;

    void layout(const UIRenderContext& ctx) override;
    void layout();

private:
    [[nodiscard]] float contentWidth() const;
    [[nodiscard]] float contentHeight() const;

    UIBoxDirection m_direction = UIBoxDirection::Vertical;
    UIJustifyContent m_justify = UIJustifyContent::Start;
    UIAlignItems m_align = UIAlignItems::Stretch;
    UIEdgeInsets m_padding{};
    float m_gap = 8.0f;
    std::unordered_map<const UIWidget*, float> m_flexGrow;
};
