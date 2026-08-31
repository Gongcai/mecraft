#pragma once

#include <array>
#include <string>

#include "../core/UIWidget.h"

struct GameResources;
class TextRenderer;

class CommandInputOverlay : public UIWidget {
public:
    void init(GameResources& resources, RhiDevice& rhiDevice) override;
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
    };

    struct CaretRect {
        int x = 0;
        int y = 0;
        int w = 2;
        int h = 1;
    };

    static ClipInfo computeClipInfo(const std::string& text, int boxX, int boxY, int boxW, int boxH, float textScale,
                                    int textPaddingX, int textPaddingY, const TextRenderer& textRenderer);
    static CaretRect computeCaretRect(const ClipInfo& info, const TextRenderer& textRenderer, float textScale);
    static bool isCaretVisible(double nowSec, float blinkPeriodMs);

    void renderBox(const std::string& text, const TextRenderer& textRenderer, const UIRenderContext& context) const;
    void drawOverlayRect(const UIRenderContext& context, int rectX, int rectY, int rectW, int rectH,
                         const std::array<float, 4>& rectColor) const;

    float m_caretBlinkPeriodMs = 530.0f;
    std::string m_text;
};
