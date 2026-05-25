#include "CommandInputOverlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../font/TextRenderer.h"
#include "../core/UIRenderUtils.h"
#include "../../core/Time.h"
#include "../../renderer/Shader.h"
#include "../../resource/ResourceMgr.h"

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

void CommandInputOverlay::setText(std::string text)
{
    m_text = std::move(text);
}

const std::string& CommandInputOverlay::getText() const
{
    return m_text;
}

void CommandInputOverlay::renderSelf(const UIRenderContext& context) const
{
    const bool show = context.commandInputVisible || visible;
    if (!show) {
        return;
    }

    const TextRenderer* textRenderer = context.textRenderer ? context.textRenderer : m_textRenderer;
    if (!textRenderer) {
        return;
    }

    const std::string* text = context.commandInputText ? context.commandInputText : &m_text;
    renderBox(*text, *textRenderer, context.theme);
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
                                                                   const TextRenderer& textRenderer)
{
    constexpr int kTextClipPadX = 10;
    constexpr int kTextClipPadY = 4;

    ClipInfo info;
    info.glyphSize = 8.0f * textScale;
    info.clipX = boxX + kTextClipPadX;
    info.clipY = boxY + kTextClipPadY;
    info.clipW = std::max(1, boxW - kTextClipPadX * 2);
    info.clipH = std::max(1, boxH - kTextClipPadY * 2);

    const float clipContentW = static_cast<float>(info.clipW) - 4.0f;

    // Find how many trailing characters fit in the clip width
    size_t maxVisibleChars = 0;
    float accumulatedW = 0.0f;
    for (size_t i = text.size(); i > 0; --i) {
        const float w = textRenderer.measureText(text.substr(i - 1, 1), textScale).width;
        if (accumulatedW + w > clipContentW) break;
        accumulatedW += w;
        maxVisibleChars++;
    }
    maxVisibleChars = std::max<size_t>(1, maxVisibleChars);

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

CommandInputOverlay::CaretRect CommandInputOverlay::computeCaretRect(const ClipInfo& info,
                                                                     const TextRenderer& textRenderer,
                                                                     float textScale)
{
    const float caretX = info.textX + textRenderer.measureText(info.visibleText, textScale).width;
    CaretRect rect;
    rect.x = static_cast<int>(std::round(std::min(caretX,
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

    // Bottom-left origin (same as inventory/text/ui_color shaders)
    const float x0 = static_cast<float>(x);
    const float y0 = static_cast<float>(y);
    const float x1 = static_cast<float>(x + w);
    const float y1 = static_cast<float>(y + h);

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
    m_crosshairShader->setVec2("uOffset", glm::vec2(0.0f, 0.0f));
    m_crosshairShader->setVec4("uColor", glm::vec4(color[0], color[1], color[2], color[3]));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(rectVerts)), rectVerts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void CommandInputOverlay::renderBox(const std::string& text, const TextRenderer& textRenderer, const UITheme* theme) const
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
    const auto clipInfo = computeClipInfo(text, x, y, boxW, boxH, textScale, textRenderer);

    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint previousScissorBox[4] = {0, 0, screenW, screenH};
    if (scissorWasEnabled) {
        glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    }

    applyOverlayBlendState();
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, screenW, screenH);

    const auto& boxCol = theme ? theme->consoleBox : std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.55f};
    const auto& textCol = theme ? theme->consoleTextNormal : std::array<float, 4>{0.95f, 0.95f, 0.95f, 1.0f};

    drawOverlayRect(screenW, screenH, x, y, boxW, boxH, boxCol);

    glScissor(clipInfo.clipX, clipInfo.clipY, clipInfo.clipW, clipInfo.clipH);
    textRenderer.render(clipInfo.visibleText,
                        clipInfo.textX,
                        clipInfo.textY,
                        textScale,
                        textCol,
                        static_cast<float>(screenW),
                        static_cast<float>(screenH));

    if (isCaretVisible(Time::getRawTime(), m_caretBlinkPeriodMs)) {
        const CaretRect caret = computeCaretRect(clipInfo, textRenderer, textScale);
        applyOverlayBlendState();
        glScissor(clipInfo.clipX, clipInfo.clipY, clipInfo.clipW, clipInfo.clipH);
        drawOverlayRect(screenW, screenH, caret.x, caret.y, caret.w, caret.h, textCol);
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
