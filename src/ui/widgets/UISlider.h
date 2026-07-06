#pragma once

#include <array>
#include <functional>

#include <cstdint>

#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "../core/Tween.h"

class Shader;

class UISlider : public UIWidget {
public:
    UISlider();
    ~UISlider() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setRange(float min, float max);
    void setValue(float value);
    [[nodiscard]] float getValue() const;
    void setStep(float step);

    void setOnValueChanged(std::function<void(float)> callback);

    void setTrackColor(const std::array<float, 4>& c) { m_trackColor = c; }
    void setFillColor(const std::array<float, 4>& c) { m_fillColor = c; }
    void setHandleColor(const std::array<float, 4>& c) { m_handleColor = c; }
    void setHandleHoverColor(const std::array<float, 4>& c) { m_handleHoverColor = c; }
    void setStyle(const UISliderStyle& style);
    void clearLocalStyle();

    void updateAnimations(float dt) override;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

private:
    [[nodiscard]] float valueToNormalized(float val) const;
    [[nodiscard]] float normalizedToValue(float norm) const;
    [[nodiscard]] float trackLeft(const UIRenderContext& ctx) const;
    [[nodiscard]] float trackRight(const UIRenderContext& ctx) const;
    [[nodiscard]] float handleScreenX(const UIRenderContext& ctx) const;
    [[nodiscard]] float pointerToValue(float px, const UIRenderContext& ctx) const;
    void applyStep();

    void initMesh();
    void cleanupMesh();
    [[nodiscard]] UISliderStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedSliderStyle resolveStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] int currentStyleState() const;

    Shader* m_shader = nullptr;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;

    float m_min = 0.0f;
    float m_max = 1.0f;
    float m_value = 0.5f;
    float m_step = 0.0f;

    std::array<float, 4> m_trackColor{0.25f, 0.25f, 0.25f, 1.0f};
    std::array<float, 4> m_fillColor{0.3f, 0.6f, 1.0f, 1.0f};
    std::array<float, 4> m_handleColor{0.85f, 0.85f, 0.85f, 1.0f};
    std::array<float, 4> m_handleHoverColor{1.0f, 1.0f, 1.0f, 1.0f};

    std::function<void(float)> m_onValueChanged;

    bool m_dragging = false;
    bool m_handleHovered = false;
    Tween<float> m_handleScaleTween;
    bool m_hasLocalStyle = false;
    UISliderStyle m_localStyle;
};
