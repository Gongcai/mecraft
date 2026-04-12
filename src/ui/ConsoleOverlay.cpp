#include "ConsoleOverlay.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "TextRenderer.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

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

void ConsoleOverlay::setVisible(bool visible)
{
    m_visible = visible;
}

bool ConsoleOverlay::isVisible() const
{
    return m_visible;
}

UIEventResult ConsoleOverlay::onInput(const UIInputEvent&)
{
    return UIEventResult::Ignored;
}

void ConsoleOverlay::render(const UIRenderContext& context) const
{
    if (!m_visible) {
        return;
    }

    const TextRenderer* textRenderer = context.textRenderer ? context.textRenderer : m_textRenderer;
    if (!textRenderer) {
        return;
    }

    render(static_cast<double>(context.timeSeconds), *textRenderer);
}

void ConsoleOverlay::drawOverlayRect(int screenW,
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

void ConsoleOverlay::render(double nowSec, const TextRenderer& textRenderer) const
{
    if (!m_visible) {
        return;
    }

    if (m_display.empty()) {
        return;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int screenW = viewport[2];
    const int screenH = viewport[3];
    if (screenW <= 0 || screenH <= 0) {
        return;
    }

    ConsoleDisplayBox::RenderParams params;
    params.screenW = screenW;
    params.screenH = screenH;
    params.visibleBoxes = m_visibleBoxes;
    params.holdSeconds = m_holdSeconds;
    params.fadeEndSeconds = m_fadeEndSeconds;
    params.textAdvanceFactor = textRenderer.getAdvanceFactor();

    m_display.setMaxLines(m_maxLines);
    m_display.render(
        nowSec,
        params,
        [this, screenW, screenH](int x, int y, int w, int h, const std::array<float, 4>& color) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            drawOverlayRect(screenW, screenH, x, y, w, h, color);
        },
        [&textRenderer, screenW, screenH](const std::string& line,
                                          float x,
                                          float y,
                                          float scale,
                                          const std::array<float, 4>& color,
                                          float,
                                          float) {
            textRenderer.render(line, x, y, scale, color, static_cast<float>(screenW), static_cast<float>(screenH));
        });
}


