#pragma once

#include <functional>
#include <string>
#include <vector>

#include "renderer/rhi/RhiHandles.h"
#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "../core/Tween.h"

class RhiDevice;

// Radio button group: a container that manages mutually exclusive options.
// Add options via addOption(), then the group handles selection and rendering.
class UIRadioButtonGroup : public UIWidget {
public:
    UIRadioButtonGroup();
    ~UIRadioButtonGroup() override;

    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

    // Add a radio option with the given label text. Returns the option index.
    int addOption(const std::string& text);

    void setSelectedIndex(int index);
    [[nodiscard]] int getSelectedIndex() const { return m_selectedIndex; }

    void setSpacing(float spacing) { m_spacing = spacing; }

    // Local color overrides.
    void setOuterColor(const Color& c) {
        m_outerColor = c;
        m_hasLocalColors = true;
    }
    void setOuterHoverColor(const Color& c) {
        m_outerHoverColor = c;
        m_hasLocalColors = true;
    }
    void setInnerColor(const Color& c) {
        m_innerColor = c;
        m_hasLocalColors = true;
    }
    void setTextColor(const Color& c) {
        m_textColor = c;
        m_hasLocalColors = true;
    }
    void setStyle(const UIRadioButtonStyle& style);
    void clearLocalStyle();

    std::function<void(int)> onSelectionChanged;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void updateAnimations(float dt) override;

private:
    struct Option {
        std::string text;
        bool hovered = false;
        Tween<float> selectTween;
    };

    void initMesh();
    void cleanupMesh();
    [[nodiscard]] int hitTestOption(float px, float py, const UIRenderContext& ctx) const;
    [[nodiscard]] UIRadioButtonStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedRadioButtonStyle resolveStyle(const UIRenderContext& ctx, bool hovered) const;

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;

    std::vector<Option> m_options;
    int m_selectedIndex = -1;
    float m_spacing = 6.0f;

    bool m_hasLocalColors = false;
    bool m_hasLocalStyle = false;
    Color m_outerColor{0.35f, 0.35f, 0.35f, 0.9f};
    Color m_outerHoverColor{0.5f, 0.5f, 0.5f, 1.0f};
    Color m_innerColor{0.2f, 0.8f, 1.0f, 1.0f};
    Color m_textColor{1.0f, 1.0f, 1.0f, 1.0f};
    UIRadioButtonStyle m_localStyle;
};
