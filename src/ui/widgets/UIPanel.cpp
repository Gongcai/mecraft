#include "UIPanel.h"

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/core/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../core/UIRenderUtils.h"

UIPanel::UIPanel() = default;
UIPanel::~UIPanel() { shutdown(); }

void UIPanel::init(ResourceMgr& resourceMgr) {
    // Create GPU resources only once per instance to prevent VAO/VBO leaks
    // when init() is called multiple times (e.g. scene re-entry).
    if (!m_gpuInitialized) {
        m_shader = resourceMgr.getShader("ui_color");
        initMesh();
        m_gpuInitialized = true;
    }
    // Always recurse into children — dynamically added children (e.g. save
    // list entries) need their own init() after being attached to the panel.
    UIWidget::init(resourceMgr);
}

void UIPanel::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
    m_gpuInitialized = false;
}

void UIPanel::setTone(UIPanelTone tone) {
    m_tone = tone;
    clearLocalColors();
}

void UIPanel::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Pre-allocate for background (6 verts) + border (24 verts) = 30 verts * 2 floats
    glBufferData(GL_ARRAY_BUFFER, 30 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIPanel::cleanupMesh() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void UIPanel::rebuildMesh(float x0, float y0, float x1, float y1) const {
    float vertices[] = {
        x0, y0,  x1, y0,  x1, y1,
        x0, y0,  x1, y1,  x0, y1,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIPanel::rebuildBorderMesh(float x0, float y0, float x1, float y1, float bw) const {
    // 4 border quads: top, bottom, left, right — each 6 vertices * 2 floats
    float vertices[48];
    // Top
    vertices[0]  = x0;      vertices[1]  = y1 - bw;  vertices[2]  = x1;      vertices[3]  = y1 - bw;
    vertices[4]  = x1;      vertices[5]  = y1;       vertices[6]  = x0;      vertices[7]  = y1 - bw;
    vertices[8]  = x1;      vertices[9]  = y1;       vertices[10] = x0;      vertices[11] = y1;
    // Bottom
    vertices[12] = x0;      vertices[13] = y0;       vertices[14] = x1;      vertices[15] = y0;
    vertices[16] = x1;      vertices[17] = y0 + bw;  vertices[18] = x0;      vertices[19] = y0;
    vertices[20] = x1;      vertices[21] = y0 + bw;  vertices[22] = x0;      vertices[23] = y0 + bw;
    // Left
    vertices[24] = x0;      vertices[25] = y0;       vertices[26] = x0 + bw; vertices[27] = y0;
    vertices[28] = x0 + bw; vertices[29] = y1;       vertices[30] = x0;      vertices[31] = y0;
    vertices[32] = x0 + bw; vertices[33] = y1;       vertices[34] = x0;      vertices[35] = y1;
    // Right
    vertices[36] = x1 - bw; vertices[37] = y0;       vertices[38] = x1;      vertices[39] = y0;
    vertices[40] = x1;      vertices[41] = y1;       vertices[42] = x1 - bw; vertices[43] = y0;
    vertices[44] = x1;      vertices[45] = y1;       vertices[46] = x1 - bw; vertices[47] = y1;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIPanel::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_vao == 0) return;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    const UIRenderUtils::GLStateGuard glState;

    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));

    // Render background
    const UIResolvedStyle resolved = UIStyleResolver::resolve(resolveBaseStyle(ctx), UIStyleState_Normal);
    std::array<float, 4> bg = resolved.background;
    bg[3] *= alpha;
    m_shader->setVec4("uColor", glm::vec4(bg[0], bg[1], bg[2], bg[3]));
    rebuildMesh(ax, ay, ax + aw, ay + ah);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Render border if width > 0 (4 quads merged into single draw call)
    float borderW = resolved.borderWidth;
    if (borderW > 0.0f) {
        std::array<float, 4> bc = resolved.border;
        bc[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(bc[0], bc[1], bc[2], bc[3]));
        rebuildBorderMesh(ax, ay, ax + aw, ay + ah, borderW);
        glDrawArrays(GL_TRIANGLES, 0, 24);
    }

    glBindVertexArray(0);
}

UIComponentStyle UIPanel::resolveBaseStyle(const UIRenderContext& ctx) const {
    UIComponentStyle style = UIStyleResolver::panelStyleFromTheme(ctx.theme, m_tone);

    if (m_hasLocalBgColor) {
        style.backgroundNormal = m_bgColor;
        style.backgroundHover = m_bgColor;
        style.backgroundPressed = m_bgColor;
        style.backgroundDisabled = m_bgColor;
    }

    if (m_hasLocalBorderColor) {
        style.borderNormal = m_borderColor;
        style.borderHover = m_borderColor;
        style.borderFocused = m_borderColor;
        style.borderPressed = m_borderColor;
        style.borderDisabled = m_borderColor;
    }

    if (m_hasLocalBorderWidth) {
        style.borderWidth = m_borderWidth;
    } else if (m_tone == UIPanelTone::Default) {
        style.borderWidth = (m_borderWidth > 0.0f) ? style.borderWidth : 0.0f;
    }

    return style;
}
