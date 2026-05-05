#pragma once

#include <array>
#include <functional>
#include <string>

#include "UIWidget.h"
#include "UIPanel.h"
#include "UIText.h"
#include "Tween.h"

class UIButton : public UIWidget {
public:
    UIButton();

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    // Configuration
    void setText(const std::string& text);
    void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

    void setNormalColor(const std::array<float, 4>& c) { m_normalColor = c; }
    void setHoverColor(const std::array<float, 4>& c) { m_hoverColor = c; }
    void setTextColor(const std::array<float, 4>& c);
    void setTextScale(float scale);

    void setHoverScale(float scale) { m_hoverTargetScale = scale; }
    void setHoverDuration(float duration) { m_hoverDuration = duration; }

    [[nodiscard]] bool isHovered() const { return m_hovered; }

    // Update animations
    void updateAnimations(float dt) override;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

private:
    UIPanel m_background;
    UIText m_label;
    std::function<void()> m_onClick;

    std::array<float, 4> m_normalColor{0.3f, 0.3f, 0.3f, 0.9f};
    std::array<float, 4> m_hoverColor{0.5f, 0.5f, 0.5f, 1.0f};

    Tween<float> m_hoverScaleTween;
    Tween<std::array<float, 4>> m_hoverColorTween;
    float m_hoverTargetScale = 1.08f;
    float m_hoverDuration = 0.15f;

    bool m_hovered = false;
    bool m_pressed = false;

    [[nodiscard]] static bool isConfirmKey(int key);
};
