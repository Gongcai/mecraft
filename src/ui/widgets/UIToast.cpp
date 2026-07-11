#include "UIToast.h"

#include <glad/glad.h>
#include <algorithm>

#include "../core/UIRenderUtils.h"
#include "../font/TextRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/core/Shader.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {

UIToastTone toToastTone(UIToast::Type type) {
    switch (type) {
    case UIToast::Type::Info:    return UIToastTone::Info;
    case UIToast::Type::Success: return UIToastTone::Success;
    case UIToast::Type::Warning: return UIToastTone::Warning;
    case UIToast::Type::Error:   return UIToastTone::Error;
    }
    return UIToastTone::Info;
}

} // namespace

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

void UIToast::setStyle(const UIToastStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UIToast::clearLocalStyle() {
    m_hasLocalStyle = false;
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

void UIToast::cleanupMesh() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

void UIToast::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_toasts.empty()) return;

    const UIRenderUtils::GLStateGuard guard;
    const UIToastStyle baseStyle = resolveBaseStyle(ctx);

    const float screenW = static_cast<float>(ctx.screenWidth);
    const float centerX = screenW * 0.5f;
    float currentY = baseStyle.bottomMargin;

    for (const auto& toast : m_toasts) {
        const float toastAlpha = toast.alphaTween.value();
        if (toastAlpha < 0.01f) continue;

        const UIResolvedToastStyle resolved = UIStyleResolver::resolveToast(baseStyle, toToastTone(toast.type));
        const float x0 = centerX - resolved.width * 0.5f;
        const float y0 = currentY;
        const float x1 = x0 + resolved.width;
        const float y1 = y0 + resolved.height;

        // Build vertices: bg (6) + border (24) + accent bar (6).
        std::vector<float> verts;
        verts.reserve(36 * 2);
        UIRenderUtils::pushColorQuad(verts, x0, y0, x1, y1); // bg
        // Border (4 quads).
        const float bw = resolved.borderWidth;
        UIRenderUtils::pushColorQuad(verts, x0, y1 - bw, x1, y1);
        UIRenderUtils::pushColorQuad(verts, x0, y0, x1, y0 + bw);
        UIRenderUtils::pushColorQuad(verts, x0, y0, x0 + bw, y1);
        UIRenderUtils::pushColorQuad(verts, x1 - bw, y0, x1, y1);
        UIRenderUtils::pushColorQuad(verts, x0, y0, x0 + resolved.accentWidth, y1);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());

        m_shader->use();
        m_shader->setVec2("uScreenSize", glm::vec2(screenW, static_cast<float>(ctx.screenHeight)));

        // Background.
        m_shader->setVec4("uColor", glm::vec4(resolved.background[0], resolved.background[1],
                                              resolved.background[2], resolved.background[3] * toastAlpha));
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Border.
        m_shader->setVec4("uColor", glm::vec4(resolved.border[0], resolved.border[1],
                                              resolved.border[2], resolved.border[3] * toastAlpha));
        glDrawArrays(GL_TRIANGLES, 6, 24);

        // Accent bar.
        m_shader->setVec4("uColor", glm::vec4(resolved.accent[0], resolved.accent[1],
                                              resolved.accent[2], resolved.accent[3] * toastAlpha));
        glDrawArrays(GL_TRIANGLES, 30, 6);

        glBindVertexArray(0);

        // Render text.
        if (ctx.textRenderer) {
            const float textScale = 1.0f;
            const auto metrics = ctx.textRenderer->measureText(toast.text, textScale);
            const float textX = x0 + resolved.textPadding;
            const float textY = y0 + (resolved.height - metrics.height) * 0.5f;
            ctx.textRenderer->render(toast.text, textX, textY, textScale,
                                     {resolved.text[0], resolved.text[1], resolved.text[2],
                                      resolved.text[3] * toastAlpha},
                                     screenW, static_cast<float>(ctx.screenHeight));
        }

        currentY += resolved.height + resolved.spacing;
    }
}

UIToastStyle UIToast::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }
    return UIStyleResolver::toastStyleFromTheme(ctx.theme);
}

UIResolvedToastStyle UIToast::resolveStyle(const UIRenderContext& ctx, Type type) const {
    return UIStyleResolver::resolveToast(resolveBaseStyle(ctx), toToastTone(type));
}
