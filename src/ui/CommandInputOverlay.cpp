#include "CommandInputOverlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "BitmapFont.h"
#include "TextRenderer.h"
#include "../core/Time.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

namespace {
void applyOverlayBlendState()
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
}

void CommandInputOverlay::init(ResourceMgr& resourceMgr)
{
    m_crosshairShader = resourceMgr.getShader("crosshair");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CommandInputOverlay::shutdown()
{
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    m_crosshairShader = nullptr;
    m_textRenderer = nullptr;
    m_text.clear();
}

UIEventResult CommandInputOverlay::onInput(const UIInputEvent&)
{
    return UIEventResult::Ignored;
}

bool CommandInputOverlay::isVisible() const
{
    return m_visible;
}

void CommandInputOverlay::setVisible(bool visible)
{
    m_visible = visible;
}

void CommandInputOverlay::setText(std::string text)
{
    m_text = std::move(text);
}

const std::string& CommandInputOverlay::getText() const
{
    return m_text;
}

void CommandInputOverlay::render(const UIRenderContext& context) const
{
    const bool visible = context.commandInputVisible || m_visible;
    if (!visible) {
        return;
    }

    const TextRenderer* textRenderer = context.textRenderer ? context.textRenderer : m_textRenderer;
    if (!textRenderer) {
        return;
    }

    const std::string* text = context.commandInputText ? context.commandInputText : &m_text;
    render(*text, *textRenderer);
}

void CommandInputOverlay::setCaretBlinkPeriodMs(float periodMs)
{
    m_caretBlinkPeriodMs = std::clamp(periodMs, 120.0f, 2500.0f);
}

float CommandInputOverlay::getCaretBlinkPeriodMs() const
{
    return m_caretBlinkPeriodMs;
}

CommandInputOverlay::ClipInfo CommandInputOverlay::computeClipInfo(const std::string& text,
                                                                   int boxX,
                                                                   int boxY,
                                                                   int boxW,
                                                                   int boxH,
                                                                   float textScale,
                                                                   float textAdvanceFactor)
{
    constexpr int kTextClipPadX = 10;
    constexpr int kTextClipPadY = 4;

    ClipInfo info;
    info.glyphSize = static_cast<float>(BitmapFont::kGlyphSizePx) * textScale;
    info.advance = info.glyphSize * textAdvanceFactor;
    info.clipX = boxX + kTextClipPadX;
    info.clipY = boxY + kTextClipPadY;
    info.clipW = std::max(1, boxW - kTextClipPadX * 2);
    info.clipH = std::max(1, boxH - kTextClipPadY * 2);

    const size_t maxVisibleChars = std::max<size_t>(
        1,
        static_cast<size_t>(std::floor((static_cast<float>(info.clipW) - 4.0f) / std::max(1.0f, info.advance))));
    const size_t visibleStart = (text.size() > maxVisibleChars) ? (text.size() - maxVisibleChars) : 0;
    info.visibleText = text.substr(visibleStart);

    info.textX = static_cast<float>(info.clipX + 2);
    info.textY = static_cast<float>(info.clipY) + (static_cast<float>(info.clipH) - info.glyphSize) * 0.5f;
    return info;
}

bool CommandInputOverlay::isCaretVisible(double nowSec, float blinkPeriodMs)
{
    const double blinkPhaseMs = std::fmod(nowSec * 1000.0, static_cast<double>(blinkPeriodMs));
    return blinkPhaseMs < static_cast<double>(blinkPeriodMs * 0.5f);
}

CommandInputOverlay::CaretRect CommandInputOverlay::computeCaretRect(const ClipInfo& info)
{
    CaretRect rect;
    rect.x = static_cast<int>(std::round(std::min(info.textX + static_cast<float>(info.visibleText.size()) * info.advance,
                                                  static_cast<float>(info.clipX + info.clipW - 3))));
    rect.y = static_cast<int>(std::floor(info.textY));
    rect.h = std::max(1, static_cast<int>(std::round(info.glyphSize)));
    return rect;
}

void CommandInputOverlay::drawOverlayRect(int screenW,
                                          int screenH,
                                          int x,
                                          int y,
                                          int w,
                                          int h,
                                          const std::array<float, 4>& color) const
{
    if (!m_crosshairShader || m_vao == 0 || m_vbo == 0 || w <= 0 || h <= 0) {
        return;
    }

    const float halfW = static_cast<float>(screenW) * 0.5f;
    const float halfH = static_cast<float>(screenH) * 0.5f;
    const float x0 = static_cast<float>(x) - halfW;
    const float y0 = static_cast<float>(y) - halfH;
    const float x1 = static_cast<float>(x + w) - halfW;
    const float y1 = static_cast<float>(y + h) - halfH;

    const float rectVerts[] = {
        x0, y0, 0.0f, 0.0f,
        x1, y0, 0.0f, 0.0f,
        x1, y1, 0.0f, 0.0f,
        x0, y0, 0.0f, 0.0f,
        x1, y1, 0.0f, 0.0f,
        x0, y1, 0.0f, 0.0f,
    };

    m_crosshairShader->use();
    m_crosshairShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(screenW), static_cast<float>(screenH)));
    m_crosshairShader->setVec4("uColor", glm::vec4(color[0], color[1], color[2], color[3]));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(rectVerts)), rectVerts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void CommandInputOverlay::render(const std::string& text, const TextRenderer& textRenderer) const
{
    if (!m_crosshairShader || m_vao == 0 || m_vbo == 0) {
        return;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int screenW = viewport[2];
    const int screenH = viewport[3];
    if (screenW <= 0 || screenH <= 0) {
        return;
    }

    const int boxW = std::max(300, std::min(screenW - 40, static_cast<int>(std::round(static_cast<float>(screenW) * 0.68f))));
    const int boxH = 34;
    const int x = 20;
    const int y = 20;
    const float textScale = 2.0f;
    const auto clipInfo = computeClipInfo(text, x, y, boxW, boxH, textScale, textRenderer.getAdvanceFactor());

    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint previousScissorBox[4] = {0, 0, screenW, screenH};
    if (scissorWasEnabled) {
        glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    }

    applyOverlayBlendState();
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, screenW, screenH);

    drawOverlayRect(screenW, screenH, x, y, boxW, boxH, {0.0f, 0.0f, 0.0f, 0.55f});

    glScissor(clipInfo.clipX, clipInfo.clipY, clipInfo.clipW, clipInfo.clipH);
    textRenderer.render(clipInfo.visibleText,
                        clipInfo.textX,
                        clipInfo.textY,
                        textScale,
                        {0.95f, 0.95f, 0.95f, 1.0f},
                        static_cast<float>(screenW),
                        static_cast<float>(screenH));

    if (isCaretVisible(Time::getRawTime(), m_caretBlinkPeriodMs)) {
        const CaretRect caret = computeCaretRect(clipInfo);
        applyOverlayBlendState();
        glScissor(clipInfo.clipX, clipInfo.clipY, clipInfo.clipW, clipInfo.clipH);
        drawOverlayRect(screenW, screenH, caret.x, caret.y, caret.w, caret.h, {0.95f, 0.95f, 0.95f, 1.0f});
    }

    if (scissorWasEnabled) {
        glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}


