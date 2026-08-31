#pragma once

#include <string>

#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "UIPanel.h"
#include "UIText.h"

// A tooltip that follows the cursor and shows after a hover delay.
// Composed of a UIPanel background and UIText label.
class UITooltip : public UIWidget {
public:
    UITooltip() = default;

    void init(GameResources& resources, RhiDevice& rhiDevice) override {
        const UIResolvedTooltipStyle style = fallbackStyle();
        m_panel.init(resources, rhiDevice);
        m_label.init(resources, rhiDevice);
        m_label.setTextScale(style.textScale);
        m_label.setTextColor(style.text);
        m_label.setShadowEnabled(true);
        m_label.setShadowColor(style.shadow);
        m_label.setShadowOffset(style.shadowOffsetX, style.shadowOffsetY);
        m_panel.setBackgroundColor(style.background);
        m_panel.setBorderColor(style.border);
        m_panel.setBorderWidth(style.borderWidth);
        visible = false;
    }

    void shutdown() override {
        m_panel.shutdown();
        m_label.shutdown();
    }

    // Begin a hover over an item. Call each frame while hovering.
    void startHover(const std::string& text, float cursorX, float cursorY, float screenW, float screenH,
                    float currentTime) const {
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
            updatePosition(fallbackStyle());
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
    void setStyle(const UITooltipStyle& style) {
        m_localStyle = style;
        m_hasLocalStyle = true;
        m_sizeDirty = true;
    }
    void clearLocalStyle() {
        m_hasLocalStyle = false;
        m_sizeDirty = true;
    }

protected:
    void renderSelf(const UIRenderContext& ctx) const override {
        if (!visible)
            return;
        const UIResolvedTooltipStyle style = resolveStyle(ctx);
        if (m_sizeDirty && ctx.textRenderer) {
            updateSize(*ctx.textRenderer, style);
            m_sizeDirty = false;
            updatePosition(style);
        }
        applyResolvedStyle(style);
        m_panel.render(ctx);
        m_label.render(ctx);
    }

private:
    [[nodiscard]] UITooltipStyle resolveBaseStyle(const UIRenderContext& ctx) const {
        if (m_hasLocalStyle) {
            return m_localStyle;
        }
        return UIStyleResolver::tooltipStyleFromTheme(ctx.theme);
    }

    [[nodiscard]] UIResolvedTooltipStyle resolveStyle(const UIRenderContext& ctx) const {
        return UIStyleResolver::resolveTooltip(resolveBaseStyle(ctx));
    }

    [[nodiscard]] UIResolvedTooltipStyle fallbackStyle() const {
        return UIStyleResolver::resolveTooltip(m_hasLocalStyle ? m_localStyle : UITooltipStyle{});
    }

    void applyResolvedStyle(const UIResolvedTooltipStyle& style) const {
        const_cast<UIPanel&>(m_panel).setBackgroundColor(style.background);
        const_cast<UIPanel&>(m_panel).setBorderColor(style.border);
        const_cast<UIPanel&>(m_panel).setBorderWidth(style.borderWidth);
        const_cast<UIText&>(m_label).setTextColor(style.text);
        const_cast<UIText&>(m_label).setTextScale(style.textScale);
        const_cast<UIText&>(m_label).setShadowColor(style.shadow);
        const_cast<UIText&>(m_label).setShadowOffset(style.shadowOffsetX, style.shadowOffsetY);
    }

    void updateSize(const TextRenderer& tr, const UIResolvedTooltipStyle& style) const {
        const_cast<UIText&>(m_label).setTextScale(style.textScale);
        const float textW = m_label.measureTextWidth(tr);
        const float textH = m_label.measureTextHeight(tr);
        width = textW + style.paddingX * 2.0f;
        height = textH + style.paddingY * 2.0f;
        m_panel.width = width;
        m_panel.height = height;
        m_label.x = style.paddingX;
        m_label.y = style.paddingY;
        m_label.width = textW;
        m_label.height = textH;
    }

    void updatePosition(const UIResolvedTooltipStyle& style) const {
        float wx = m_cursorX + style.offsetX;
        float wy = (m_screenH - m_cursorY) - style.offsetY;

        if (wx + width > m_screenW - style.margin) {
            wx = m_cursorX - width - style.offsetX;
        }
        if (wy < style.margin) {
            wy = style.margin;
        }
        if (wy + height > m_screenH - style.margin) {
            wy = m_screenH - style.margin - height;
        }

        m_panel.anchor = Anchor::BottomLeft;
        m_panel.anchorOffsetX = 0;
        m_panel.anchorOffsetY = 0;
        m_panel.x = wx;
        m_panel.y = wy;

        m_label.anchor = Anchor::BottomLeft;
        m_label.anchorOffsetX = 0;
        m_label.anchorOffsetY = 0;
        m_label.x = wx + style.paddingX;
        m_label.y = wy + style.paddingY;
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

    bool m_hasLocalStyle = false;
    UITooltipStyle m_localStyle;
};
