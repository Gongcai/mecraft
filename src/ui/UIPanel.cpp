#include "UIPanel.h"

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

UIPanel::UIPanel() = default;
UIPanel::~UIPanel() { shutdown(); }

void UIPanel::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
}

void UIPanel::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
}

void UIPanel::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
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
    // Two triangles forming a quad
    float vertices[] = {
        x0, y0,
        x1, y0,
        x1, y1,
        x0, y0,
        x1, y1,
        x0, y1,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIPanel::renderSelf(const UIRenderContext& ctx) const {
    if (!m_shader || m_vao == 0) return;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float aw = width * scaleX;
    float ah = height * scaleY;

    // Save GL state
    GLboolean depthTest;
    glGetBooleanv(GL_DEPTH_TEST, &depthTest);
    GLint blendSrc, blendDst;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDst);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(static_cast<float>(ctx.screenWidth),
                                                static_cast<float>(ctx.screenHeight)));

    // Render background
    std::array<float, 4> bg = m_bgColor;
    bg[3] *= alpha;
    m_shader->setVec4("uColor", glm::vec4(bg[0], bg[1], bg[2], bg[3]));
    rebuildMesh(ax, ay, ax + aw, ay + ah);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Render border if width > 0
    if (m_borderWidth > 0.0f) {
        std::array<float, 4> bc = m_borderColor;
        bc[3] *= alpha;
        m_shader->setVec4("uColor", glm::vec4(bc[0], bc[1], bc[2], bc[3]));
        float bw = m_borderWidth;

        // Top border
        rebuildMesh(ax, ay + ah - bw, ax + aw, ay + ah);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // Bottom border
        rebuildMesh(ax, ay, ax + aw, ay + bw);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // Left border
        rebuildMesh(ax, ay, ax + bw, ay + ah);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // Right border
        rebuildMesh(ax + aw - bw, ay, ax + aw, ay + ah);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);

    // Restore GL state
    if (depthTest) glEnable(GL_DEPTH_TEST);
    glBlendFunc(blendSrc, blendDst);
}
