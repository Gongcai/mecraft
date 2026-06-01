#include "UIToast.h"

#include <glad/glad.h>
#include <algorithm>

#include "../core/UIRenderUtils.h"
#include "../core/UITheme.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

UIToast::UIToast() {
    interactive = false;
    focusable = false;
    visible = true;
}

UIToast::~UIToast() {
    shutdown();
}

void UIToast::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    // Buffer for: bg (6 verts) + border (24 verts) per toast, up to maxVisible toasts.
    constexpr int maxToasts = 8;
    constexpr int totalFloats = maxToasts * 30 * 2;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(totalFloats * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    UIWidget::init(resourceMgr);
}

void UIToast::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
    m_toasts.clear();
    UIWidget::shutdown();
}

void UIToast::showToast(const std::string& text, Type type, float duration) {
    ToastEntry entry;
    entry.text = text;
    entry.type = type;
    entry.elapsed = 0.0f;
    entry.duration = duration;
    entry.alphaTween.start(0.0f, 1.0f, 0.2f, EasingType::EaseOut);
    m_toasts.insert(m_toasts.begin(), std::move(entry));

    // Remove excess toasts.
    while (static_cast<int>(m_toasts.size()) > m_maxVisible) {
        m_toasts.pop_back();
    }
}

Color UIToast::getToastColor(const UITheme* theme, Type type) const {
    if (theme) {
        switch (type) {
        case Type::Info:    return theme->toastInfo;
        case Type::Success: return theme->toastSuccess;
        case Type::Warning: return theme->toastWarning;
        case Type::Error:   return theme->toastError;
        }
    }
    switch (type) {
    case Type::Info:    return {0.2f, 0.8f, 1.0f, 1.0f};
    case Type::Success: return {0.3f, 0.7f, 0.3f, 1.0f};
    case Type::Warning: return {0.9f, 0.7f, 0.2f, 1.0f};
    case Type::Error:   return {0.7f, 0.3f, 0.3f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

void UIToast::onUpdate(float dt) {
    // Update toast timers and animations.
    for (auto it = m_toasts.begin(); it != m_toasts.end();) {
        it->elapsed += dt;
        it->alphaTween.tick(dt);

        // Start fade-out when nearing the end.
        const float fadeStart = it->duration - 0.3f;
        if (it->elapsed >= fadeStart && it->alphaTween.value() > 0.0f) {
            // Only start the fade-out tween once.
            if (it->alphaTween.value() > 0.9f) {
                it->alphaTween.start(1.0f, 0.0f, 0.3f, EasingType::EaseIn);
            }
        }

        if (it->elapsed >= it->duration) {
            it = m_toasts.erase(it);
        } else {
            ++it;
        }
    }
}

void UIToast::initMesh() {
    // Already initialized in init().
}

void UIToast::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

void UIToast::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_toasts.empty()) return;

    const UIRenderUtils::GLStateGuard guard;

    const Color bgCol  = ctx.theme ? ctx.theme->toastBackground : Color{0.15f, 0.15f, 0.15f, 0.92f};
    const Color txtCol = ctx.theme ? ctx.theme->toastText       : Color{1.0f, 1.0f, 1.0f, 1.0f};
    const float toastW = ctx.theme ? ctx.theme->toastWidth  : 300.0f;
    const float toastH = ctx.theme ? ctx.theme->toastHeight : 40.0f;
    const float spacing = 8.0f;
    const float bottomMargin = 60.0f;

    const float screenW = static_cast<float>(ctx.screenWidth);
    const float centerX = screenW * 0.5f;
    float currentY = bottomMargin;

    for (const auto& toast : m_toasts) {
        const float toastAlpha = toast.alphaTween.value();
        if (toastAlpha < 0.01f) continue;

        const Color accentCol = getToastColor(ctx.theme, toast.type);
        const float x0 = centerX - toastW * 0.5f;
        const float y0 = currentY;
        const float x1 = x0 + toastW;
        const float y1 = y0 + toastH;

        // Build vertices: bg (6) + border (24) + accent bar (6).
        std::vector<float> verts;
        verts.reserve(36 * 2);
        UIRenderUtils::pushColorQuad(verts, x0, y0, x1, y1); // bg
        // Border (4 quads).
        const float bw = 1.0f;
        UIRenderUtils::pushColorQuad(verts, x0, y1 - bw, x1, y1);
        UIRenderUtils::pushColorQuad(verts, x0, y0, x1, y0 + bw);
        UIRenderUtils::pushColorQuad(verts, x0, y0, x0 + bw, y1);
        UIRenderUtils::pushColorQuad(verts, x1 - bw, y0, x1, y1);
        // Left accent bar (3px wide).
        UIRenderUtils::pushColorQuad(verts, x0, y0, x0 + 3.0f, y1);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

        m_shader->use();
        m_shader->setVec2("uScreenSize", glm::vec2(screenW, static_cast<float>(ctx.screenHeight)));

        // Background.
        m_shader->setVec4("uColor", glm::vec4(bgCol[0], bgCol[1], bgCol[2], bgCol[3] * toastAlpha));
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Border.
        m_shader->setVec4("uColor", glm::vec4(bgCol[0] * 1.3f, bgCol[1] * 1.3f, bgCol[2] * 1.3f, 0.5f * toastAlpha));
        glDrawArrays(GL_TRIANGLES, 6, 24);

        // Accent bar.
        m_shader->setVec4("uColor", glm::vec4(accentCol[0], accentCol[1], accentCol[2], accentCol[3] * toastAlpha));
        glDrawArrays(GL_TRIANGLES, 30, 6);

        glBindVertexArray(0);

        // Render text.
        if (ctx.textRenderer) {
            const float textScale = 1.0f;
            const auto metrics = ctx.textRenderer->measureText(toast.text, textScale);
            const float textX = x0 + 10.0f;
            const float textY = y0 + (toastH - metrics.height) * 0.5f;
            ctx.textRenderer->render(toast.text, textX, textY, textScale,
                                     {txtCol[0], txtCol[1], txtCol[2], txtCol[3] * toastAlpha},
                                     screenW, static_cast<float>(ctx.screenHeight));
        }

        currentY += toastH + spacing;
    }
}
