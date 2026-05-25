#include "UIImage.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/core/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../core/UIRenderUtils.h"

void UIImage::init(ResourceMgr& resourceMgr) {
    m_inventoryShader = resourceMgr.getShader("inventory");
    m_colorShader = resourceMgr.getShader("ui_color");
    initMesh();
}

void UIImage::shutdown() {
    cleanupMesh();
    m_inventoryShader = nullptr;
    m_colorShader = nullptr;
}

void UIImage::loadTexture(ResourceMgr& resourceMgr, const std::string& name, const std::string& path) {
    int imgW = 0, imgH = 0;
    GLuint tex = resourceMgr.loadGuiTexture(name, path, imgW, imgH);
    setTexture(tex, 0.0f, 0.0f, 1.0f, 1.0f);
    if (imgW > 0 && imgH > 0) {
        width = static_cast<float>(imgW);
        height = static_cast<float>(imgH);
    }
}

void UIImage::setAtlasTile(const TextureAtlas& atlas, int tileIndex) {
    if (atlas.textureID == 0 || tileIndex < 0) {
        m_textureID = 0;
        m_useTexture = false;
        return;
    }
    m_textureID = atlas.textureID;
    const auto uv = atlas.getUV(tileIndex);
    m_u0 = uv.first.x;
    m_v0 = uv.first.y;
    m_u1 = uv.second.x;
    m_v1 = uv.second.y;
    m_useTexture = true;
}

void UIImage::setTexture(GLuint textureID, float u0, float v0, float u1, float v1) {
    m_textureID = textureID;
    m_u0 = u0;
    m_v0 = v0;
    m_u1 = u1;
    m_v1 = v1;
    m_useTexture = (textureID != 0);
}

void UIImage::setSolidColor(const std::array<float, 4>& c) {
    m_tintColor = c;
    m_useTexture = false;
}

void UIImage::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // 6 vertices * 4 floats (x, y, u, v)
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIImage::cleanupMesh() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void UIImage::rebuildMesh(float x0, float y0, float x1, float y1,
                           float u0, float v0, float u1, float v1) const {
    float vertices[] = {
        x0, y0, u0, v0,
        x1, y0, u1, v0,
        x1, y1, u1, v1,
        x0, y0, u0, v0,
        x1, y1, u1, v1,
        x0, y1, u0, v1,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIImage::renderSelf(const UIRenderContext& ctx) const {
    if (m_vao == 0) return;

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;

    const UIRenderUtils::GLStateGuard glState;

    if (m_useTexture && m_textureID != 0 && m_inventoryShader) {
        m_inventoryShader->use();
        m_inventoryShader->setVec2("uScreenSize",
                                   glm::vec2(static_cast<float>(ctx.screenWidth),
                                             static_cast<float>(ctx.screenHeight)));
        std::array<float, 4> tint = m_tintColor;
        tint[3] *= alpha;
        m_inventoryShader->setVec4("uTintColor",
                                   glm::vec4(tint[0], tint[1], tint[2], tint[3]));
        m_inventoryShader->setInt("uAtlas", 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textureID);

        rebuildMesh(ax, ay, ax + aw, ay + ah, m_u0, m_v0, m_u1, m_v1);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else if (!m_useTexture && m_colorShader) {
        m_colorShader->use();
        m_colorShader->setVec2("uScreenSize",
                               glm::vec2(static_cast<float>(ctx.screenWidth),
                                         static_cast<float>(ctx.screenHeight)));
        std::array<float, 4> tint = m_tintColor;
        tint[3] *= alpha;
        m_colorShader->setVec4("uColor",
                               glm::vec4(tint[0], tint[1], tint[2], tint[3]));

        rebuildMesh(ax, ay, ax + aw, ay + ah, 0.0f, 0.0f, 1.0f, 1.0f);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
}
