#pragma once

#include <array>
#include <functional>
#include <string>

#include <glad/glad.h>

#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "../core/Tween.h"
#include "UIText.h"

class Shader;

// Sliding toggle switch widget with on/off states and smooth animation.
class UIToggle : public UIWidget {
public:
    UIToggle();
    ~UIToggle() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const { return m_checked; }

    void setLabel(const std::string& text);
    void setLabelTextScale(float scale);

    // Local color overrides.
    void setTrackOffColor(const Color& c) { m_trackOffColor = c; m_hasLocalColors = true; }
    void setTrackOnColor(const Color& c)  { m_trackOnColor = c; m_hasLocalColors = true; }
    void setKnobColor(const Color& c)     { m_knobColor = c; m_hasLocalColors = true; }
    void setKnobHoverColor(const Color& c){ m_knobHoverColor = c; m_hasLocalColors = true; }
    void setStyle(const UIToggleStyle& style);
    void clearLocalStyle();

    std::function<void(bool)> onChanged;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void updateAnimations(float dt) override;
    void setFocused(bool focused) override;

private:
    void initMesh();
    void cleanupMesh();
    void toggle();
    [[nodiscard]] UIToggleStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] int currentStyleState() const;

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    bool m_checked = false;
    bool m_hovered = false;
    UIText m_label;
    Tween<float> m_knobTween;

    bool m_hasLocalColors = false;
    Color m_trackOffColor{0.25f, 0.25f, 0.25f, 0.9f};
    Color m_trackOnColor{0.2f, 0.7f, 0.4f, 1.0f};
    Color m_knobColor{0.9f, 0.9f, 0.9f, 1.0f};
    Color m_knobHoverColor{1.0f, 1.0f, 1.0f, 1.0f};
    bool m_hasLocalStyle = false;
    UIToggleStyle m_localStyle;
};
