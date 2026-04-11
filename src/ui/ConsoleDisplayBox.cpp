#include "ConsoleDisplayBox.h"

#include <algorithm>
#include <cmath>

#include <glad/glad.h>
#include "BitmapFont.h"

namespace {
std::array<float, 4> textColorForType(ConsoleDisplayBox::MessageType type,
                                      const ConsoleDisplayBox::RenderParams& params)
{
    switch (type) {
        case ConsoleDisplayBox::MessageType::Warning:
            return params.warningTextColor;
        case ConsoleDisplayBox::MessageType::Success:
            return params.successTextColor;
        case ConsoleDisplayBox::MessageType::Normal:
        default:
            return params.normalTextColor;
    }
}
} // namespace

void ConsoleDisplayBox::appendLine(const std::string& message,
                                   double createdAtSec,
                                   MessageType type)
{
    if (message.empty()) {
        return;
    }

    m_lines.push_back(Line{message, createdAtSec, type});
    while (m_lines.size() > m_maxLines) {
        m_lines.pop_front();
    }
}

void ConsoleDisplayBox::clear()
{
    m_lines.clear();
}

void ConsoleDisplayBox::setMaxLines(std::size_t maxLines)
{
    m_maxLines = std::max<std::size_t>(1, maxLines);
    while (m_lines.size() > m_maxLines) {
        m_lines.pop_front();
    }
}

bool ConsoleDisplayBox::empty() const
{
    return m_lines.empty();
}

void ConsoleDisplayBox::pruneExpired(double nowSec, float fadeEndSeconds)
{
    const double fadeEnd = std::max(0.001, static_cast<double>(fadeEndSeconds));
    while (!m_lines.empty()) {
        const double age = nowSec - m_lines.front().createdAtSec;
        if (age < fadeEnd) {
            break;
        }
        m_lines.pop_front();
    }
}

float ConsoleDisplayBox::computeLineAlpha(double ageSec, float holdSeconds, float fadeEndSeconds)
{
    if (ageSec >= static_cast<double>(fadeEndSeconds)) {
        return 0.0f;
    }

    if (ageSec <= static_cast<double>(holdSeconds)) {
        return 1.0f;
    }

    const double fadeDuration = std::max(0.001, static_cast<double>(fadeEndSeconds - holdSeconds));
    const float alpha = static_cast<float>(1.0 - ((ageSec - static_cast<double>(holdSeconds)) / fadeDuration));
    return std::clamp(alpha, 0.0f, 1.0f);
}

void ConsoleDisplayBox::render(double nowSec,
                               const RenderParams& params,
                               const DrawRectFn& drawRect,
                               const RenderTextFn& renderText)
{
    if (!drawRect || !renderText || params.screenW <= 0 || params.screenH <= 0) {
        return;
    }

    pruneExpired(nowSec, params.fadeEndSeconds);
    if (m_lines.empty()) {
        return;
    }

    const int boxW = std::max(params.minBoxW,
                              std::min(params.screenW - params.horizontalMargin * 2,
                                       static_cast<int>(std::round(static_cast<float>(params.screenW) * params.boxWidthRatio))));
    const int clipW = std::max(1, boxW - params.textPadX * 2);
    const int clipH = std::max(1, params.boxH - params.textPadY * 2);
    const float glyphSize = static_cast<float>(BitmapFont::kGlyphSizePx) * params.textScale;
    const float advance = glyphSize * params.textAdvanceFactor;
    const std::size_t maxVisibleChars = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::floor((static_cast<float>(clipW) - 4.0f) / std::max(1.0f, advance))));

    const int stackBaseY = params.inputY + params.inputBoxH + params.inputToFirstBoxGap;

    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint previousScissorBox[4] = {0, 0, params.screenW, params.screenH};
    if (scissorWasEnabled) {
        glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, params.screenW, params.screenH);

    const std::size_t visibleCount = std::min(params.visibleBoxes, m_lines.size());
    std::size_t drawnCount = 0;
    for (auto it = m_lines.rbegin(); it != m_lines.rend() && drawnCount < visibleCount; ++it) {
        const double age = nowSec - it->createdAtSec;
        const float alpha = computeLineAlpha(age, params.holdSeconds, params.fadeEndSeconds);
        if (alpha <= 0.0f) {
            continue;
        }

        const int boxY = stackBaseY + static_cast<int>(drawnCount) * (params.boxH + params.boxGap);

        glScissor(0, 0, params.screenW, params.screenH);
        auto bgColor = params.boxColor;
        bgColor[3] *= alpha;
        drawRect(params.x, boxY, boxW, params.boxH, bgColor);

        const std::size_t visibleStart = (it->text.size() > maxVisibleChars) ? (it->text.size() - maxVisibleChars) : 0;
        const std::string visibleText = it->text.substr(visibleStart);

        const int clipX = params.x + params.textPadX;
        const int clipY = boxY + params.textPadY;
        const float textX = static_cast<float>(clipX + 2);
        const float textY = static_cast<float>(clipY) + (static_cast<float>(clipH) - glyphSize) * 0.5f;

        glScissor(clipX, clipY, clipW, clipH);
        auto tintedTextColor = textColorForType(it->type, params);
        tintedTextColor[3] *= alpha;
        renderText(visibleText,
                   textX,
                   textY,
                   params.textScale,
                   tintedTextColor,
                   static_cast<float>(params.screenW),
                   static_cast<float>(params.screenH));

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ++drawnCount;
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

