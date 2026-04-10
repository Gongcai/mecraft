#include "UIRenderer.h"
#include <algorithm>
#include <cmath>
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"
#include "../renderer/Shader.h"
#include "../player/Inventory.h"
#include "../world/Block.h"

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer()
{
    shutdown();
}

void UIRenderer::init(ResourceMgr& resourceMgr)
{
    m_crosshairShader = resourceMgr.getShader("crosshair");
    m_inventoryShader = resourceMgr.getShader("inventory");
    m_resourceMgr = &resourceMgr;
    initCrosshairMesh();
    initHotbarMesh();
}

void UIRenderer::shutdown()
{
    cleanupCrosshairMesh();
    cleanupHotbarMesh();
    m_crosshairShader = nullptr;
    m_inventoryShader = nullptr;
    m_resourceMgr = nullptr;
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

void UIRenderer::setHotbarBgColor(const std::array<float, 4>& color)
{
    m_hotbarBgColor = color;
}

const std::array<float, 4>& UIRenderer::getHotbarBgColor() const
{
    return m_hotbarBgColor;
}

void UIRenderer::setHotbarBorderColor(const std::array<float, 4>& color)
{
    m_hotbarBorderColor = color;
}

const std::array<float, 4>& UIRenderer::getHotbarBorderColor() const
{
    return m_hotbarBorderColor;
}

void UIRenderer::setHotbarIconTintColor(const std::array<float, 4>& color)
{
    m_hotbarIconTintColor = color;
}

const std::array<float, 4>& UIRenderer::getHotbarIconTintColor() const
{
    return m_hotbarIconTintColor;
}

// ─── Crosshair ───

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

// ─── Hotbar ───

void UIRenderer::initHotbarMesh()
{
    glGenVertexArrays(1, &m_hotbarVao);
    glGenBuffers(1, &m_hotbarVbo);

    glBindVertexArray(m_hotbarVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_hotbarVbo);
    // Max: 1 bg + 1 selected slot + 9 icons = 11 quads * 6 verts * 4 floats
    glBufferData(GL_ARRAY_BUFFER, 11 * 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIRenderer::cleanupHotbarMesh()
{
    if (m_hotbarVao != 0)
    {
        glDeleteVertexArrays(1, &m_hotbarVao);
        m_hotbarVao = 0;
    }
    if (m_hotbarVbo != 0)
    {
        glDeleteBuffers(1, &m_hotbarVbo);
        m_hotbarVbo = 0;
    }
}

void UIRenderer::renderHotbar(const Window& window, const Inventory& inventory)
{
    if (!m_inventoryShader || !m_resourceMgr)
        return;

    const float screenW = static_cast<float>(window.getWidth());
    const float screenH = static_cast<float>(window.getHeight());
    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    const GLuint widgetsTexture = m_resourceMgr->getGuiTexture("widgets");
    if (widgetsTexture == 0)
        return;

    const bool hasBakedItemIcons = (itemIconAtlas.textureID != 0 && itemIconAtlas.tilesPerRow > 0);
    const bool hasLegacyAtlas = (atlas.textureID != 0 && atlas.tilesPerRow > 0);
    if (!hasBakedItemIcons && !hasLegacyAtlas)
        return;

    constexpr float kWidgetsWidth = 182.0f;
    constexpr float kWidgetsHeight = 46.0f;
    constexpr float kBgHeight = 21.0f;
    constexpr float kHighlightSize = 25.0f;
    constexpr float kScale = 2.0f;
    constexpr int hotbarSlots = Inventory::HOTBAR_SIZE;
    const float hotbarWidth = kWidgetsWidth * kScale;
    const float hotbarHeight = kBgHeight * kScale;

    const float startX = (screenW - hotbarWidth) * 0.5f;
    const float startY = 8.0f;

    const int selectedSlot = std::clamp(inventory.getSelectedSlot(), 0, hotbarSlots - 1);

    auto addQuad = [](std::vector<float>& buf,
                      float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1)
    {
        buf.push_back(x0); buf.push_back(y0); buf.push_back(u0); buf.push_back(v0);
        buf.push_back(x1); buf.push_back(y0); buf.push_back(u1); buf.push_back(v0);
        buf.push_back(x1); buf.push_back(y1); buf.push_back(u1); buf.push_back(v1);
        buf.push_back(x0); buf.push_back(y0); buf.push_back(u0); buf.push_back(v0);
        buf.push_back(x1); buf.push_back(y1); buf.push_back(u1); buf.push_back(v1);
        buf.push_back(x0); buf.push_back(y1); buf.push_back(u0); buf.push_back(v1);
    };

    auto uvFromTopLeftPixels = [=](float x0, float y0, float x1, float y1)
    {
        // widgets.png is loaded with vertical flip, so convert top-left pixel rects to bottom-left UVs.
        const float u0 = x0 / kWidgetsWidth;
        const float u1 = x1 / kWidgetsWidth;
        const float v0 = (kWidgetsHeight - y1) / kWidgetsHeight;
        const float v1 = (kWidgetsHeight - y0) / kWidgetsHeight;
        return std::array<float, 4>{u0, v0, u1, v1};
    };

    // Layer 1: widgets hotbar background.
    std::vector<float> bgVerts;
    {
        // New widgets layout: background uses (row,col) [0..20] x [1..181]
        // converted to (x0,y0,x1,y1) = (1,0,182,21).
        const auto uv = uvFromTopLeftPixels(1.0f, 0.0f, 182.0f, 21.0f);
        addQuad(bgVerts,
                startX, startY,
                startX + hotbarWidth, startY + hotbarHeight,
                uv[0], uv[1], uv[2], uv[3]);
    }

    // Layer 2: widgets selected slot frame.
    std::vector<float> selectedVerts;
    {
        // New widgets layout: highlight uses (row,col) [21..45] x [0..24]
        // converted to (x0,y0,x1,y1) = (0,21,25,46).
        const auto uv = uvFromTopLeftPixels(0.0f, 21.0f, 25.0f, 46.0f);
        const float slotStride = 20.0f * kScale;
        const float selectorOffset = ((kHighlightSize - 20.0f) * 0.5f) * kScale;
        const float selX = startX + static_cast<float>(selectedSlot) * slotStride - selectorOffset + 2;
        const float selY = startY - 3.0f;
        addQuad(selectedVerts,
                selX, selY,
                selX + kHighlightSize * kScale, selY + kHighlightSize * kScale,
                uv[0], uv[1], uv[2], uv[3]);
    }

    // Layer 3: Block icons from the block atlas.
    std::vector<float> iconVerts;
    constexpr float slotStride = 20.0f * kScale;
    constexpr float iconInset = 2.0f * kScale;
    constexpr float iconSize = 17.5f * kScale;
    for (int i = 0; i < hotbarSlots; ++i)
    {
        BlockID blockId = inventory.getSlot(i);
        if (blockId == BlockType::AIR) continue;

        glm::vec2 uvMin;
        glm::vec2 uvMax;
        if (hasBakedItemIcons)
        {
            const auto uv = itemIconAtlas.getUV(static_cast<int>(blockId));
            uvMin = uv.first;
            uvMax = uv.second;
        }
        else
        {
            const BlockDef& blockDef = BlockRegistry::get(blockId);
            int tileIndex = blockDef.texFront;
            if (tileIndex < 0)
                tileIndex = blockDef.texTop;
            if (tileIndex < 0)
                continue;

            const auto uv = atlas.getUV(tileIndex);
            uvMin = uv.first;
            uvMax = uv.second;
        }

        const float sx = startX + static_cast<float>(i) * slotStride;
        const float ix = sx + iconInset;
        float iy = startY + iconInset;

        addQuad(iconVerts, ix, iy, ix + iconSize, iy + iconSize, uvMin.x, uvMin.y, uvMax.x, uvMax.y);
    }

    // Combine all layers into one VBO: bg | selected | icons
    std::vector<float> vertices;
    vertices.reserve(bgVerts.size() + selectedVerts.size() + iconVerts.size());
    vertices.insert(vertices.end(), bgVerts.begin(), bgVerts.end());
    vertices.insert(vertices.end(), selectedVerts.begin(), selectedVerts.end());
    vertices.insert(vertices.end(), iconVerts.begin(), iconVerts.end());

    int bgVertCount = static_cast<int>(bgVerts.size() / 4);
    int selectedVertCount = static_cast<int>(selectedVerts.size() / 4);
    int iconVertCount = static_cast<int>(iconVerts.size() / 4);

    glBindBuffer(GL_ARRAY_BUFFER, m_hotbarVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // ── Draw ──
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(screenW, screenH));

    glActiveTexture(GL_TEXTURE0);
    m_inventoryShader->setInt("uAtlas", 0);

    glBindVertexArray(m_hotbarVao);

    int offset = 0;

    // Draw textured background.
    glBindTexture(GL_TEXTURE_2D, widgetsTexture);
    m_inventoryShader->setVec4("uTintColor", glm::vec4(m_hotbarBgColor[0], m_hotbarBgColor[1], m_hotbarBgColor[2], m_hotbarBgColor[3]));
    glDrawArrays(GL_TRIANGLES, offset, bgVertCount);
    offset += bgVertCount;

    // Draw textured selected frame.
    m_inventoryShader->setVec4("uTintColor", glm::vec4(m_hotbarBorderColor[0], m_hotbarBorderColor[1], m_hotbarBorderColor[2], m_hotbarBorderColor[3]));
    glDrawArrays(GL_TRIANGLES, offset, selectedVertCount);
    offset += selectedVertCount;

    // Draw block icons.
    glBindTexture(GL_TEXTURE_2D, hasBakedItemIcons ? itemIconAtlas.textureID : atlas.textureID);
    m_inventoryShader->setVec4("uTintColor", glm::vec4(m_hotbarIconTintColor[0], m_hotbarIconTintColor[1], m_hotbarIconTintColor[2], m_hotbarIconTintColor[3]));
    glDrawArrays(GL_TRIANGLES, offset, iconVertCount);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void UIRenderer::render(const Window& window, const Inventory& inventory)
{
    renderCrosshair(window);
    renderHotbar(window, inventory);
}
