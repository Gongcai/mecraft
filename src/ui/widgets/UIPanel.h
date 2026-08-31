#pragma once

#include <array>
#include "../core/UIStyle.h"
#include "../core/UIWidget.h"

class UIPanel : public UIWidget {
public:
    UIPanel();
    ~UIPanel() override;

    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

    void setBackgroundColor(const std::array<float, 4>& c) {
        m_bgColor = c;
        m_hasLocalBgColor = true;
    }
    [[nodiscard]] const std::array<float, 4>& getBackgroundColor() const { return m_bgColor; }
    void setUseLocalColors(bool v) {
        m_hasLocalBgColor = v;
        m_hasLocalBorderColor = v;
        m_hasLocalBorderWidth = v;
    }
    void clearLocalColors() { setUseLocalColors(false); }
    void setTone(UIPanelTone tone);

    void setBorderColor(const std::array<float, 4>& c) {
        m_borderColor = c;
        m_hasLocalBorderColor = true;
    }
    [[nodiscard]] const std::array<float, 4>& getBorderColor() const { return m_borderColor; }

    void setBorderWidth(float w) {
        m_borderWidth = w;
        m_hasLocalBorderWidth = true;
    }
    [[nodiscard]] float getBorderWidth() const { return m_borderWidth; }

protected:
    void renderSelf(const UIRenderContext& ctx) const override;

private:
    [[nodiscard]] UIComponentStyle resolveBaseStyle(const UIRenderContext& ctx) const;

    std::array<float, 4> m_bgColor{0.2f, 0.2f, 0.2f, 0.8f};
    std::array<float, 4> m_borderColor{1.0f, 1.0f, 1.0f, 0.5f};
    float m_borderWidth = 0.0f;
    bool m_hasLocalBgColor = false;
    bool m_hasLocalBorderColor = false;
    bool m_hasLocalBorderWidth = false;
    UIPanelTone m_tone = UIPanelTone::Default;
};
