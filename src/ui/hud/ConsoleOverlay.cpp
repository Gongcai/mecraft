#include "ConsoleOverlay.h"

#include <algorithm>
#include <cmath>

#include <glad/glad.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../font/TextRenderer.h"
#include "../core/UIRenderUtils.h"
#include "../core/UIStyle.h"
#include "../../renderer/core/Shader.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/ResourceMgr.h"

void ConsoleOverlay::init(ResourceMgr& resourceMgr)
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

void ConsoleOverlay::shutdown()
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
    m_display.clear();
}

void ConsoleOverlay::appendLine(const std::string& message,
                                double createdAtSec,
                                ConsoleDisplayBox::MessageType type)
{
    if (message.empty()) {
        return;
    }

    m_display.setMaxLines(m_maxLines);
    m_display.appendLine(message, createdAtSec, type);
}

void ConsoleOverlay::clear()
{
    m_display.clear();
}

bool ConsoleOverlay::empty() const
{
    return m_display.empty();
}

void ConsoleOverlay::setMaxLines(std::size_t maxLines)
{
    m_maxLines = maxLines;
    m_display.setMaxLines(maxLines);
}

void ConsoleOverlay::setTextRenderer(const TextRenderer* textRenderer)
{
    m_textRenderer = textRenderer;
}

void ConsoleOverlay::renderSelf(const UIRenderContext& context) const
{
    const TextRenderer* textRenderer = context.textRenderer ? context.textRenderer : m_textRenderer;
    if (!textRenderer) {
        return;
    }

    renderMessages(static_cast<double>(context.timeSeconds), *textRenderer, context);
}

void ConsoleOverlay::drawOverlayRect(int screenW,
                                     int screenH,
                                     int rectX,
                                     int rectY,
                                     int rectW,
                                     int rectH,
                                     const std::array<float, 4>& rectColor) const
{
    if (!m_crosshairShader || m_vao == 0 || m_vbo == 0 || rectW <= 0 || rectH <= 0) {
        return;
    }

    // Bottom-left origin (same as inventory/text/ui_color shaders)
    const float x0 = static_cast<float>(rectX);
    const float y0 = static_cast<float>(rectY);
    const float x1 = static_cast<float>(rectX + rectW);
    const float y1 = static_cast<float>(rectY + rectH);

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
    m_crosshairShader->setVec4("uColor", glm::vec4(rectColor[0], rectColor[1], rectColor[2], rectColor[3]));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(rectVerts)), rectVerts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ConsoleOverlay::renderMessages(double nowSec, const TextRenderer& textRenderer,
                                    const UIRenderContext& context) const
{
    if (m_display.empty() || context.commandList == nullptr) {
        return;
    }

    const int screenW = context.screenWidth;
    const int screenH = context.screenHeight;
    if (screenW <= 0 || screenH <= 0) {
        return;
    }

    ConsoleDisplayBox::RenderParams params;
    params.screenW = screenW;
    params.screenH = screenH;
    params.visibleBoxes = m_visibleBoxes;
    params.holdSeconds = m_holdSeconds;
    params.fadeEndSeconds = m_fadeEndSeconds;
    const UIResolvedConsoleStyle style =
        UIStyleResolver::resolveConsole(UIStyleResolver::consoleStyleFromTheme(context.theme));
    params.x = style.x;
    params.inputY = style.inputY;
    params.inputBoxH = style.inputBoxHeight;
    params.inputToFirstBoxGap = style.inputToFirstBoxGap;
    params.boxH = style.boxHeight;
    params.boxGap = style.boxGap;
    params.horizontalMargin = style.horizontalMargin;
    params.minBoxW = style.minBoxWidth;
    params.boxWidthRatio = style.boxWidthRatio;
    params.textPadX = style.textPaddingX;
    params.textPadY = style.textPaddingY;
    params.textScale = style.textScale;
    params.boxColor = style.box;
    params.normalTextColor = style.textNormal;
    params.warningTextColor = style.textWarning;
    params.successTextColor = style.textSuccess;

    m_display.setMaxLines(m_maxLines);
    m_display.render(
        nowSec,
        params,
        [this, screenW, screenH](int rectX, int rectY, int rectW, int rectH, const std::array<float, 4>& rectColor) {
            const UIRenderUtils::GLStateGuard glState;
            drawOverlayRect(screenW, screenH, rectX, rectY, rectW, rectH, rectColor);
        },
        [&context](int clipX, int clipY, int clipW, int clipH) {
            const float uiScale = context.pixelScale();
            RhiRect2D clip{
                static_cast<int32_t>(std::floor(static_cast<float>(clipX) * uiScale)),
                static_cast<int32_t>(std::floor(static_cast<float>(clipY) * uiScale)),
                static_cast<uint32_t>(std::max(0.0f, std::ceil(static_cast<float>(clipW) * uiScale))),
                static_cast<uint32_t>(std::max(0.0f, std::ceil(static_cast<float>(clipH) * uiScale)))
            };
            if (context.hasScissor) {
                const int32_t x0 = std::max(clip.x, context.scissor.x);
                const int32_t y0 = std::max(clip.y, context.scissor.y);
                const int32_t x1 = std::min(clip.x + static_cast<int32_t>(clip.width),
                                            context.scissor.x + static_cast<int32_t>(context.scissor.width));
                const int32_t y1 = std::min(clip.y + static_cast<int32_t>(clip.height),
                                            context.scissor.y + static_cast<int32_t>(context.scissor.height));
                clip = {x0, y0, static_cast<uint32_t>(std::max(0, x1 - x0)),
                        static_cast<uint32_t>(std::max(0, y1 - y0))};
            }
            context.commandList->setScissor(clip);
        },
        [&textRenderer, screenW, screenH](const std::string& line,
                                          float textX,
                                          float textY,
                                          float scale,
                                          const std::array<float, 4>& textColor,
                                          float,
                                          float) {
            textRenderer.render(line,
                                textX,
                                textY,
                                scale,
                                textColor,
                                static_cast<float>(screenW),
                                static_cast<float>(screenH));
        },
        [&textRenderer](const std::string& text, float scale) -> ConsoleDisplayBox::TextMetricsResult {
            auto m = textRenderer.measureText(text, scale);
            return {m.width, m.height};
        });

    const float uiScale = context.pixelScale();
    const RhiRect2D parentScissor = context.hasScissor
        ? context.scissor
        : RhiRect2D{0, 0, static_cast<uint32_t>(screenW * uiScale),
                    static_cast<uint32_t>(screenH * uiScale)};
    context.commandList->setScissor(parentScissor);
}
