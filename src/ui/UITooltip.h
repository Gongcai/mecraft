#pragma once

#include <string>

#include "UIWidget.h"
#include "UIPanel.h"
#include "UIText.h"
#include "UITheme.h"

// A tooltip that follows the cursor and shows after a hover delay.
// Composed of a UIPanel background and UIText label.
class UITooltip : public UIWidget {
public:
    UITooltip() = default;

    void init(ResourceMgr& resourceMgr) override {
        m_panel.init(resourceMgr);
        m_label.init(resourceMgr);
        m_label.setTextScale(2.0f);
        m_label.setTextColor({1.0f, 1.0f, 1.0f, 1.0f});
        m_label.setShadowEnabled(true);
        m_label.setShadowColor({0.0f, 0.0f, 0.0f, 0.75f});
        m_label.setShadowOffset(1.0f, -1.0f);
        m_panel.setBackgroundColor({0.15f, 0.15f, 0.15f, 0.92f});
        m_panel.setBorderColor({0.4f, 0.4f, 0.4f, 0.8f});
        m_panel.setBorderWidth(1.0f);
        visible = false;
    }

    void shutdown() override {
        m_panel.shutdown();
        m_label.shutdown();
    }

    // Begin a hover over an item. Call each frame while hovering.
    void startHover(const std::string& text, float cursorX, float cursorY,
                    float screenW, float screenH, float currentTime) const {
        if (m_hoverText != text) {
            m_hoverText = text;
            m_hoverStartTime = currentTime;
            m_label.setText(text);
            m_sizeDirty = true;
        }
        m_cursorX = cursorX;
        m_cursorY = cursorY;
        m_screenW = screenW;
        m_screenH = screenH;
        m_hovering = true;

        if (!visible && (currentTime - m_hoverStartTime) >= m_hoverDelay) {
            visible = true;
        }
        if (visible) {
            updatePosition();
        }
    }

    // Cancel hover (cursor left the item).
    void cancelHover() const {
        m_hovering = false;
        visible = false;
        m_hoverText.clear();
    }

    [[nodiscard]] bool isHovering() const { return m_hovering; }

    void setHoverDelay(float seconds) { m_hoverDelay = seconds; }
    [[nodiscard]] float getHoverDelay() const { return m_hoverDelay; }

protected:
    void renderSelf(const UIRenderContext& ctx) const override {
        if (!visible) return;
        if (m_sizeDirty && ctx.textRenderer) {
            updateSize(*ctx.textRenderer);
            m_sizeDirty = false;
            updatePosition();
        }
        // Apply tooltip-specific theme colors before rendering sub-widgets
        if (ctx.theme) {
            const_cast<UIPanel&>(m_panel).setBackgroundColor(ctx.theme->tooltipBackground);
            const_cast<UIPanel&>(m_panel).setBorderColor(ctx.theme->tooltipBorder);
            const_cast<UIPanel&>(m_panel).setBorderWidth(ctx.theme->tooltipBorderWidth);
            const_cast<UIText&>(m_label).setTextColor(ctx.theme->textPrimary);
        }
        m_panel.render(ctx);
        m_label.render(ctx);
    }

private:
    void updateSize(const TextRenderer& tr) const {
        const float textW = m_label.measureTextWidth(tr);
        const float textH = m_label.measureTextHeight(tr);
        constexpr float padX = 10.0f;
        constexpr float padY = 6.0f;
        width = textW + padX * 2.0f;
        height = textH + padY * 2.0f;
        m_panel.width = width;
        m_panel.height = height;
        m_label.x = padX;
        m_label.y = padY;
        m_label.width = textW;
        m_label.height = textH;
    }

    void updatePosition() const {
        constexpr float offsetX = 12.0f;
        constexpr float offsetY = 16.0f;
        constexpr float margin = 4.0f;

        float wx = m_cursorX + offsetX;
        float wy = (m_screenH - m_cursorY) - offsetY;

        if (wx + width > m_screenW - margin) {
            wx = m_cursorX - width - offsetX;
        }
        if (wy < margin) {
            wy = margin;
        }
        if (wy + height > m_screenH - margin) {
            wy = m_screenH - margin - height;
        }

        m_panel.anchor = Anchor::BottomLeft;
        m_panel.anchorOffsetX = 0;
        m_panel.anchorOffsetY = 0;
        m_panel.x = wx;
        m_panel.y = wy;

        m_label.anchor = Anchor::BottomLeft;
        m_label.anchorOffsetX = 0;
        m_label.anchorOffsetY = 0;
        m_label.x = wx + 10.0f;
        m_label.y = wy + 6.0f;
    }

    mutable UIPanel m_panel;
    mutable UIText m_label;

    mutable std::string m_hoverText;
    float m_hoverDelay = 0.3f;
    mutable float m_hoverStartTime = 0.0f;
    mutable bool m_hovering = false;
    mutable bool m_sizeDirty = false;

    mutable float m_cursorX = 0.0f;
    mutable float m_cursorY = 0.0f;
    mutable float m_screenW = 0.0f;
    mutable float m_screenH = 0.0f;
};
