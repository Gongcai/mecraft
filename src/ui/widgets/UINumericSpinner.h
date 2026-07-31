#pragma once

#include <array>
#include <functional>
#include <string>

#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "renderer/rhi/RhiHandles.h"

class RhiDevice;

// Numeric spinner widget with -/+ buttons and a value display area.
// Supports direct keyboard input when the value area is clicked.
class UINumericSpinner : public UIWidget {
public:
    UINumericSpinner();
    ~UINumericSpinner() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setValue(float value);
    [[nodiscard]] float getValue() const { return m_value; }

    void setRange(float min, float max);
    void setStep(float step) { m_step = step; }
    [[nodiscard]] float getStep() const { return m_step; }

    // Number of decimal places to display (0 = integer).
    void setDecimals(int decimals) { m_decimals = decimals; }
    [[nodiscard]] int getDecimals() const { return m_decimals; }

    void setStyle(const UINumericSpinnerStyle& style);
    void clearLocalStyle();

    std::function<void(float)> onValueChanged;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void onUpdate(float dt) override;

private:
    void initMesh();
    void cleanupMesh();
    void applyStep(float delta);
    void commitEditText();
    [[nodiscard]] std::string formatValue() const;
    [[nodiscard]] int hitTestZone(float px, float py, const UIRenderContext& ctx) const;
    [[nodiscard]] UINumericSpinnerStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedNumericSpinnerStyle resolveStyle(const UIRenderContext& ctx) const;
    // Returns: -1 = minus button, 0 = value area, 1 = plus button, -2 = outside.

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;

    float m_value = 0.0f;
    float m_min = 0.0f;
    float m_max = 100.0f;
    float m_step = 1.0f;
    int m_decimals = 0;

    bool m_minusHovered = false;
    bool m_plusHovered = false;
    bool m_editing = false;
    std::string m_editText;
    float m_cursorBlinkTimer = 0.0f;
    bool m_cursorVisible = true;
    static constexpr float kCursorBlinkRate = 1.0f;

    // Backspace auto-repeat state.
    float m_backspaceHoldElapsed = 0.0f;
    float m_backspaceRepeatAccum = 0.0f;
    bool m_backspaceHeld = false;
    static constexpr float kBackspaceInitialDelay = 0.28f;
    static constexpr float kBackspaceRepeatInterval = 0.05f;

    bool m_hasLocalStyle = false;
    UINumericSpinnerStyle m_localStyle;
};
