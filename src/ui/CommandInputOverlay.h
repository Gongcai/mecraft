#pragma once

#include <array>
#include <string>

#include <glad/glad.h>

#include "UIWidget.h"

class ResourceMgr;
class Shader;
class TextRenderer;

class CommandInputOverlay : public UIWidget
{
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setText(std::string text);
    [[nodiscard]] const std::string& getText() const;

    void setCaretBlinkPeriodMs(float periodMs);
    [[nodiscard]] float getCaretBlinkPeriodMs() const;

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    struct ClipInfo {
        std::string visibleText;
        int clipX = 0;
        int clipY = 0;
        int clipW = 1;
        int clipH = 1;
        float textX = 0.0f;
        float textY = 0.0f;
        float glyphSize = 0.0f;
        float advance = 1.0f;
    };

    struct CaretRect {
        int x = 0;
        int y = 0;
        int w = 2;
        int h = 1;
    };

    static ClipInfo computeClipInfo(const std::string& text,
                                    int boxX,
                                    int boxY,
                                    int boxW,
                                    int boxH,
                                    float textScale,
                                    float textAdvanceFactor);
    static CaretRect computeCaretRect(const ClipInfo& info);
    static bool isCaretVisible(double nowSec, float blinkPeriodMs);

    void renderBox(const std::string& text, const TextRenderer& textRenderer) const;
    void drawOverlayRect(int screenW,
                         int screenH,
                         int x,
                         int y,
                         int w,
                         int h,
                         const std::array<float, 4>& color) const;

    Shader* m_crosshairShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    float m_caretBlinkPeriodMs = 530.0f;
    std::string m_text;
    const TextRenderer* m_textRenderer = nullptr;
};

