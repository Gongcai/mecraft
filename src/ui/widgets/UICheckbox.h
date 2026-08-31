#pragma once

#include <array>
#include <functional>
#include <string>

#include "renderer/rhi/RhiHandles.h"
#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "UIText.h"
#include "../core/Tween.h"

class RhiDevice;

class UICheckbox : public UIWidget {
public:
    UICheckbox();
    ~UICheckbox() override;

    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const;

    void setLabel(const std::string& text);
    void setOnChanged(std::function<void(bool)> callback);
    void setStyle(const UICheckboxStyle& style);
    void clearLocalStyle();

    void updateAnimations(float dt) override;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

private:
    void initMesh();
    void cleanupMesh();
    [[nodiscard]] UICheckboxStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] int currentStyleState() const;

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_shapeFragmentShader;
    RhiShaderHandle m_colorFragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_shapePipeline;
    RhiPipelineHandle m_colorPipeline;
    UIText m_label;

    bool m_checked = false;
    bool m_hovered = false;

    std::array<float, 4> m_boxColor{0.25f, 0.25f, 0.25f, 0.9f};
    std::array<float, 4> m_boxHoverColor{0.35f, 0.35f, 0.35f, 1.0f};
    std::array<float, 4> m_boxBorderColor{0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> m_checkColor{0.3f, 0.8f, 0.4f, 1.0f};
    bool m_hasLocalStyle = false;
    UICheckboxStyle m_localStyle;

    std::function<void(bool)> m_onChanged;
    Tween<float> m_checkScaleTween;
};
