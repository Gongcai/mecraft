#include "UIRenderer.h"
#include <algorithm>
#include <cmath>
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"
#include "../renderer/Shader.h"

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer()
{
    shutdown();
}

void UIRenderer::init(ResourceMgr& resourceMgr)
{
    m_crosshairShader = resourceMgr.getShader("crosshair");
    initCrosshairMesh();
}

void UIRenderer::shutdown()
{
    cleanupCrosshairMesh();
    m_crosshairShader = nullptr;
}

void UIRenderer::setCrosshairSize(float size)
{
    const float clampedSize = std::clamp(size, 0.5f, 4.0f);
    if (m_crosshairSize == clampedSize)
        return;

    m_crosshairSize = clampedSize;
    rebuildCrosshairMesh();
}

float UIRenderer::getCrosshairSize() const
{
    return m_crosshairSize;
}

void UIRenderer::setCrosshairColor(const std::array<float, 4>& color)
{
    m_crosshairColor = color;
}

const std::array<float, 4>& UIRenderer::getCrosshairColor() const
{
    return m_crosshairColor;
}

void UIRenderer::initCrosshairMesh()
{
    constexpr int kBaseArmLen = 7;
    constexpr int kBaseThickness = 2;

    const int armLen = std::max(2, static_cast<int>(std::lround(kBaseArmLen * m_crosshairSize)));
    const int thickness = std::max(2, static_cast<int>(std::lround(kBaseThickness * m_crosshairSize)));
    const int halfT = thickness / 2;

    std::vector<float> vertices;
    auto addQuad = [&](int x0, int y0, int x1, int y1)
    {
        auto fx0 = static_cast<float>(x0);
        auto fy0 = static_cast<float>(y0);
        auto fx1 = static_cast<float>(x1);
        auto fy1 = static_cast<float>(y1);

        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy1);
    };

    // 5 个不重叠矩形：中心块 + 上下左右四个支臂
    addQuad(-halfT, -halfT, halfT, halfT);
    addQuad(-armLen - halfT, -halfT, -halfT, halfT);
    addQuad(halfT, -halfT, armLen + halfT, halfT);
    addQuad(-halfT, -armLen - halfT, halfT, -halfT);
    addQuad(-halfT, halfT, halfT, armLen + halfT);

    m_vertexCount = static_cast<int>(vertices.size() / 2);

    glGenVertexArrays(1, &m_crosshairVao);
    glGenBuffers(1, &m_crosshairVbo);

    glBindVertexArray(m_crosshairVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_crosshairVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIRenderer::rebuildCrosshairMesh()
{
    if (m_crosshairVao == 0 && m_crosshairVbo == 0)
    {
        initCrosshairMesh();
        return;
    }

    cleanupCrosshairMesh();
    initCrosshairMesh();
}

void UIRenderer::cleanupCrosshairMesh()
{
    if (m_crosshairVao != 0)
    {
        glDeleteVertexArrays(1, &m_crosshairVao);
        m_crosshairVao = 0;
    }
    if (m_crosshairVbo != 0)
    {
        glDeleteBuffers(1, &m_crosshairVbo);
        m_crosshairVbo = 0;
    }
    m_vertexCount = 0;
}

void UIRenderer::render(const Window& window)
{
    renderCrosshair(window);
}

void UIRenderer::renderCrosshair(const Window& window)
{
    if (!m_crosshairShader || m_vertexCount == 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);

    m_crosshairShader->use();
    m_crosshairShader->setVec4("uColor", glm::vec4(m_crosshairColor[0], m_crosshairColor[1], m_crosshairColor[2], m_crosshairColor[3]));
    m_crosshairShader->setVec2("uScreenSize", {static_cast<float>(window.getWidth()), static_cast<float>(window.getHeight())});

    glBindVertexArray(m_crosshairVao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);

    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
