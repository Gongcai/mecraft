#include "UIProgressBar.h"

#include <glad/glad.h>
#include <algorithm>
#include <cstdio>
#include <vector>

#include "../core/UIRenderUtils.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

UIProgressBar::UIProgressBar() {
    interactive = false;
    focusable = false;
    width = 200.0f;
    height = 20.0f;
}

UIProgressBar::~UIProgressBar() {
    shutdown();
}

void UIProgressBar::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
    m_progressTween.start(0.0f, m_progress, 0.3f, EasingType::EaseOut);
    UIWidget::init(resourceMgr);
}

void UIProgressBar::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
    UIWidget::shutdown();
}

void UIProgressBar::setProgress(float progress) {
    m_progress = std::clamp(progress, 0.0f, 1.0f);
    m_progressTween.start(m_progressTween.value(), m_progress, 0.3f, EasingType::EaseOut);
}

void UIProgressBar::setLabel(const std::string& label) {
    m_label = label;
}

void UIProgressBar::setTone(UIProgressBarTone tone) {
    m_tone = tone;
    m_hasLocalColors = false;
    m_hasLocalStyle = false;
}

void UIProgressBar::setStyle(const UIProgressBarStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIProgressBar::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UIProgressBar::updateAnimations(float dt) {
    m_progressTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UIProgressBar::initMesh() {
    constexpr int kMaxShapeVerts = 160;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxShapeVerts * 2 * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void UIProgressBar::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

void UIProgressBar::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader) return;

    const UIRenderUtils::GLStateGuard guard;

    const UIResolvedProgressBarStyle resolved = resolveStyle(ctx);
    const Color trackCol = resolved.track;
    const Color fillCol = resolved.fill;
    const Color textCol = resolved.text;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    const float progressVal = std::clamp(m_progressTween.value(), 0.0f, 1.0f);
    const float fillWidth = aw * progressVal;

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    m_shader->use();
    m_shader->setVec2("uScreenSize",
                      glm::vec2(static_cast<float>(ctx.screenWidth),
                                static_cast<float>(ctx.screenHeight)));

    auto drawShape = [&](const std::vector<float>& verts, Color shapeColor) {
        if (verts.empty()) return;
        shapeColor[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(shapeColor[0], shapeColor[1], shapeColor[2], shapeColor[3]));
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                        verts.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 2));
    };

    std::vector<float> verts;
    verts.reserve(160);

    verts.clear();
    UIRenderUtils::pushCapsule(verts, ax, ay, ax + aw, ay + ah);
    drawShape(verts, trackCol);

    if (fillWidth > 0.5f) {
        verts.clear();
        UIRenderUtils::pushCapsule(verts, ax, ay, ax + fillWidth, ay + ah);
        drawShape(verts, fillCol);
    }

    glBindVertexArray(0);

    // Render text overlay.
    if (ctx.textRenderer) {
        std::string overlayText;
        if (!m_label.empty()) {
            overlayText = m_label;
        } else if (m_showPercent) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f%%", progressVal * 100.0f);
            overlayText = buf;
        }

        if (!overlayText.empty()) {
            const float fontPixelHeight = resolved.fontPixelHeight > 0.0f ? resolved.fontPixelHeight : 32.0f;
            const float textScale = (ah * resolved.textHeightRatio) / fontPixelHeight;
            const auto metrics = ctx.textRenderer->measureText(overlayText, textScale);
            const float textX = ax + (aw - metrics.width) * 0.5f;
            const float textY = ay + (ah - metrics.height) * 0.5f;
            ctx.textRenderer->render(overlayText, textX, textY, textScale,
                                     {textCol[0], textCol[1], textCol[2], textCol[3] * alpha},
                                     static_cast<float>(ctx.screenWidth),
                                     static_cast<float>(ctx.screenHeight));
        }
    }
}

UIProgressBarStyle UIProgressBar::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UIProgressBarStyle style = UIStyleResolver::progressBarStyleFromTheme(ctx.theme, m_tone);
    if (m_hasLocalColors) {
        style.track = m_trackColor;
        style.fill = m_fillColor;
        style.text = m_textColor;
    }
    return style;
}

UIResolvedProgressBarStyle UIProgressBar::resolveStyle(const UIRenderContext& ctx) const {
    return UIStyleResolver::resolveProgressBar(resolveBaseStyle(ctx));
}
