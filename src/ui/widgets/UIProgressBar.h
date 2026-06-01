#pragma once

#include <array>
#include <string>

#include <glad/glad.h>

#include "../core/UIWidget.h"
#include "../core/Tween.h"

class Shader;

// Progress bar widget that displays a 0..1 progress value with optional text overlay.
class UIProgressBar : public UIWidget {
public:
    UIProgressBar();
    ~UIProgressBar() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    // Set the target progress value (0.0 .. 1.0). Animates smoothly to the new value.
    void setProgress(float progress);
    [[nodiscard]] float getProgress() const { return m_progress; }

    // Set the displayed label text (e.g. "Loading..."). Empty string hides the label.
    void setLabel(const std::string& label);
    [[nodiscard]] const std::string& getLabel() const { return m_label; }

    // Show or hide the percentage text overlay.
    void setShowPercent(bool show) { m_showPercent = show; }
    [[nodiscard]] bool getShowPercent() const { return m_showPercent; }

    // Local color overrides (bypass theme when set).
    void setTrackColor(const Color& c) { m_trackColor = c; m_hasLocalColors = true; }
    void setFillColor(const Color& c) { m_fillColor = c; m_hasLocalColors = true; }
    void setTextColor(const Color& c) { m_textColor = c; m_hasLocalColors = true; }

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    void updateAnimations(float dt) override;

private:
    void initMesh();
    void cleanupMesh();

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    float m_progress = 0.0f;
    Tween<float> m_progressTween;
    std::string m_label;
    bool m_showPercent = false;
    bool m_hasLocalColors = false;

    Color m_trackColor{0.2f, 0.2f, 0.2f, 0.9f};
    Color m_fillColor{0.2f, 0.8f, 1.0f, 1.0f};
    Color m_textColor{1.0f, 1.0f, 1.0f, 1.0f};
};
