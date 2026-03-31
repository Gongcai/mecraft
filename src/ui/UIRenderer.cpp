#include "UIRenderer.h"
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

void UIRenderer::initCrosshairMesh()
{
    const int armLen = 7;
    const int thickness = 2;
    const int halfT = thickness / 2;

    std::vector<float> vertices;
    auto addQuad = [&](int x0, int y0, int x1, int y1)
    {
        float fx0 = static_cast<float>(x0);
        float fy0 = static_cast<float>(y0);
        float fx1 = static_cast<float>(x1);
        float fy1 = static_cast<float>(y1);

        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy0);
        vertices.push_back(fx1); vertices.push_back(fy1);
        vertices.push_back(fx0); vertices.push_back(fy1);
    };

    // 对称坐标：中心区域从 -halfT 到 +halfT，臂从中心向外延伸 armLen
    addQuad(-armLen - halfT, -halfT, armLen + halfT, halfT);
    addQuad(-halfT, -armLen - halfT, halfT, armLen + halfT);

    m_vertexCount = static_cast<int>(vertices.size() / 2);

    glGenVertexArrays(1, &m_crosshairVao);
    glGenBuffers(1, &m_crosshairVbo);

    glBindVertexArray(m_crosshairVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_crosshairVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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
    m_crosshairShader->setVec2("uScreenSize", {static_cast<float>(window.getWidth()), static_cast<float>(window.getHeight())});

    glBindVertexArray(m_crosshairVao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);

    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
