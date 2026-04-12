#include "CrosshairControl.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../core/Window.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

void CrosshairControl::init(ResourceMgr& resourceMgr)
{
    m_shader = resourceMgr.getShader("crosshair");
    initMesh();
}

void CrosshairControl::shutdown()
{
    cleanupMesh();
    m_shader = nullptr;
}

void CrosshairControl::setSize(float size)
{
    const float clamped = std::clamp(size, 0.5f, 4.0f);
    if (m_size == clamped) {
        return;
    }

    m_size = clamped;
    rebuildMesh();
}

float CrosshairControl::getSize() const
{
    return m_size;
}

void CrosshairControl::setColor(const std::array<float, 4>& color)
{
    m_color = color;
}

const std::array<float, 4>& CrosshairControl::getColor() const
{
    return m_color;
}

void CrosshairControl::initMesh()
{
    constexpr int kBaseArmLen = 7;
    constexpr int kBaseThickness = 2;

    const int armLen = std::max(2, static_cast<int>(std::lround(kBaseArmLen * m_size)));
    const int thickness = std::max(2, static_cast<int>(std::lround(kBaseThickness * m_size)));
    const int halfT = thickness / 2;

    std::vector<float> vertices;
    auto addQuad = [&](int x0, int y0, int x1, int y1)
    {
        const auto fx0 = static_cast<float>(x0);
        const auto fy0 = static_cast<float>(y0);
        const auto fx1 = static_cast<float>(x1);
        const auto fy1 = static_cast<float>(y1);

        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy1);
    };

    addQuad(-halfT, -halfT, halfT, halfT);
    addQuad(-armLen - halfT, -halfT, -halfT, halfT);
    addQuad(halfT, -halfT, armLen + halfT, halfT);
    addQuad(-halfT, -armLen - halfT, halfT, -halfT);
    addQuad(-halfT, halfT, halfT, armLen + halfT);

    m_vertexCount = static_cast<int>(vertices.size() / 2);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CrosshairControl::rebuildMesh()
{
    if (m_vao == 0 && m_vbo == 0) {
        return;
    }
    cleanupMesh();
    initMesh();
}

void CrosshairControl::cleanupMesh()
{
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    m_vertexCount = 0;
}

void CrosshairControl::render(const Window& window) const
{
    if (!m_shader || m_vertexCount == 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->use();
    m_shader->setVec4("uColor", glm::vec4(m_color[0], m_color[1], m_color[2], m_color[3]));
    m_shader->setVec2("uScreenSize", {static_cast<float>(window.getWidth()), static_cast<float>(window.getHeight())});

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

