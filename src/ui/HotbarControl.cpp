#include "HotbarControl.h"

#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../core/Window.h"
#include "../player/Inventory.h"
#include "../resource/ResourceMgr.h"
#include "../renderer/Shader.h"
#include "../world/Block.h"
#include "TextRenderer.h"

void HotbarControl::init(ResourceMgr& resourceMgr)
{
    m_inventoryShader = resourceMgr.getShader("inventory");
    m_resourceMgr = &resourceMgr;
    initMesh();
}

void HotbarControl::shutdown()
{
    cleanupMesh();
    m_inventoryShader = nullptr;
    m_resourceMgr = nullptr;
    m_inventory = nullptr;
}

void HotbarControl::setInventorySource(const Inventory* inventory)
{
    m_inventory = inventory;
}

void HotbarControl::setVisible(bool visible)
{
    m_visible = visible;
}

bool HotbarControl::isVisible() const
{
    return m_visible;
}

UIEventResult HotbarControl::onInput(const UIInputEvent&)
{
    return UIEventResult::Ignored;
}

void HotbarControl::render(const UIRenderContext& context) const
{
    if (!m_visible) {
        return;
    }

    const Inventory* inventory = context.inventory ? context.inventory : m_inventory;
    if (!inventory || context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    renderInternal(static_cast<float>(context.screenWidth),
                   static_cast<float>(context.screenHeight),
                   *inventory);

    // Render item name popup above hotbar
    if (context.textRenderer) {
        checkSlotChange(*inventory);
        renderItemName(static_cast<float>(context.screenWidth),
                       static_cast<float>(context.screenHeight),
                       *inventory,
                       *context.textRenderer,
                       context.timeSeconds);
    }
}

void HotbarControl::initMesh()
{
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 11 * 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HotbarControl::cleanupMesh()
{
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void HotbarControl::setBgColor(const std::array<float, 4>& color)
{
    m_bgColor = color;
}

const std::array<float, 4>& HotbarControl::getBgColor() const
{
    return m_bgColor;
}

void HotbarControl::setBorderColor(const std::array<float, 4>& color)
{
    m_borderColor = color;
}

const std::array<float, 4>& HotbarControl::getBorderColor() const
{
    return m_borderColor;
}

void HotbarControl::setIconTintColor(const std::array<float, 4>& color)
{
    m_iconTintColor = color;
}

const std::array<float, 4>& HotbarControl::getIconTintColor() const
{
    return m_iconTintColor;
}

void HotbarControl::render(const Window& window, const Inventory& inventory) const
{
    if (!m_visible) {
        return;
    }

    renderInternal(static_cast<float>(window.getWidth()),
                   static_cast<float>(window.getHeight()),
                   inventory);
}

void HotbarControl::renderInternal(float screenW, float screenH, const Inventory& inventory) const
{
    if (!m_inventoryShader || !m_resourceMgr || m_vao == 0 || m_vbo == 0) {
        return;
    }
    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    const GLuint widgetsTexture = m_resourceMgr->getGuiTexture("widgets");
    if (widgetsTexture == 0) {
        return;
    }

    const bool hasBakedItemIcons = (itemIconAtlas.textureID != 0 && itemIconAtlas.tilesPerRow > 0);
    const bool hasLegacyAtlas = (atlas.textureID != 0 && atlas.tilesPerRow > 0);
    if (!hasBakedItemIcons && !hasLegacyAtlas) {
        return;
    }

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
        const float u0 = x0 / kWidgetsWidth;
        const float u1 = x1 / kWidgetsWidth;
        const float v0 = (kWidgetsHeight - y1) / kWidgetsHeight;
        const float v1 = (kWidgetsHeight - y0) / kWidgetsHeight;
        return std::array<float, 4>{u0, v0, u1, v1};
    };

    std::vector<float> bgVerts;
    {
        const auto uv = uvFromTopLeftPixels(1.0f, 0.0f, 182.0f, 21.0f);
        addQuad(bgVerts,
                startX, startY,
                startX + hotbarWidth, startY + hotbarHeight,
                uv[0], uv[1], uv[2], uv[3]);
    }

    std::vector<float> selectedVerts;
    {
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

    std::vector<float> iconVerts;
    constexpr float slotStride = 20.0f * kScale;
    constexpr float iconInset = 2.0f * kScale;
    constexpr float iconSize = 17.5f * kScale;
    for (int i = 0; i < hotbarSlots; ++i)
    {
        BlockID blockId = inventory.getSlot(i);
        if (blockId == BlockType::AIR) {
            continue;
        }

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
            if (tileIndex < 0) {
                tileIndex = blockDef.texTop;
            }
            if (tileIndex < 0) {
                continue;
            }

            const auto uv = atlas.getUV(tileIndex);
            uvMin = uv.first;
            uvMax = uv.second;
        }

        const float sx = startX + static_cast<float>(i) * slotStride;
        const float ix = sx + iconInset;
        const float iy = startY + iconInset;

        addQuad(iconVerts, ix, iy, ix + iconSize, iy + iconSize, uvMin.x, uvMin.y, uvMax.x, uvMax.y);
    }

    std::vector<float> vertices;
    vertices.reserve(bgVerts.size() + selectedVerts.size() + iconVerts.size());
    vertices.insert(vertices.end(), bgVerts.begin(), bgVerts.end());
    vertices.insert(vertices.end(), selectedVerts.begin(), selectedVerts.end());
    vertices.insert(vertices.end(), iconVerts.begin(), iconVerts.end());

    const int bgVertCount = static_cast<int>(bgVerts.size() / 4);
    const int selectedVertCount = static_cast<int>(selectedVerts.size() / 4);
    const int iconVertCount = static_cast<int>(iconVerts.size() / 4);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(screenW, screenH));

    glActiveTexture(GL_TEXTURE0);
    m_inventoryShader->setInt("uAtlas", 0);

    glBindVertexArray(m_vao);

    int offset = 0;

    glBindTexture(GL_TEXTURE_2D, widgetsTexture);
    m_inventoryShader->setVec4("uTintColor", glm::vec4(m_bgColor[0], m_bgColor[1], m_bgColor[2], m_bgColor[3]));
    glDrawArrays(GL_TRIANGLES, offset, bgVertCount);
    offset += bgVertCount;

    m_inventoryShader->setVec4("uTintColor", glm::vec4(m_borderColor[0], m_borderColor[1], m_borderColor[2], m_borderColor[3]));
    glDrawArrays(GL_TRIANGLES, offset, selectedVertCount);
    offset += selectedVertCount;

    glBindTexture(GL_TEXTURE_2D, hasBakedItemIcons ? itemIconAtlas.textureID : atlas.textureID);
    m_inventoryShader->setVec4("uTintColor", glm::vec4(m_iconTintColor[0], m_iconTintColor[1], m_iconTintColor[2], m_iconTintColor[3]));
    glDrawArrays(GL_TRIANGLES, offset, iconVertCount);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void HotbarControl::checkSlotChange(const Inventory& inventory) const
{
    const int currentSlot = inventory.getSelectedSlot();
    const BlockID currentBlock = inventory.getSelectedBlock();

    if (m_lastSelectedSlot < 0) {
        // First frame, just record state without showing popup
        m_lastSelectedSlot = currentSlot;
        m_lastSelectedBlock = currentBlock;
        return;
    }

    if (currentSlot != m_lastSelectedSlot || currentBlock != m_lastSelectedBlock) {
        m_lastSelectedSlot = currentSlot;
        m_lastSelectedBlock = currentBlock;
        if (currentBlock != BlockType::AIR) {
            const BlockDef& blockDef = BlockRegistry::get(currentBlock);
            m_itemName = blockDef.name;
        } else {
            m_itemName.clear();
        }
        m_itemNameShowTime = 0.0f; // will be set to current time in renderItemName
    }
}

void HotbarControl::renderItemName(float screenW, float screenH, const Inventory& inventory, const TextRenderer& textRenderer, float timeSeconds) const
{
    if (m_itemName.empty()) {
        return;
    }

    // Set show time on first call after change
    if (m_itemNameShowTime <= -1.0f) {
        return;
    }
    if (m_itemNameShowTime == 0.0f) {
        m_itemNameShowTime = timeSeconds;
    }

    const float elapsed = timeSeconds - m_itemNameShowTime;
    if (elapsed > m_itemNameDisplayDuration) {
        m_itemNameShowTime = -100.0f;
        return;
    }

    // Fade out in the last 0.5 seconds
    float alpha = 1.0f;
    constexpr float kFadeDuration = 0.5f;
    if (elapsed > m_itemNameDisplayDuration - kFadeDuration) {
        alpha = (m_itemNameDisplayDuration - elapsed) / kFadeDuration;
        alpha = std::clamp(alpha, 0.0f, 1.0f);
    }

    // Position: centered above the hotbar
    constexpr float kWidgetsWidth = 182.0f;
    constexpr float kScale = 2.0f;
    constexpr float kBgHeight = 21.0f;
    const float hotbarWidth = kWidgetsWidth * kScale;
    const float hotbarHeight = kBgHeight * kScale;
    const float hotbarY = 8.0f;
    const float hotbarCenterX = screenW * 0.5f;

    const float textScale = 1.5f;
    const float textY = hotbarY + hotbarHeight + 6.0f;

    const std::array<float, 4> textColor = {1.0f, 1.0f, 1.0f, alpha};

    // Estimate text width for centering
    const float advanceFactor = textRenderer.getAdvanceFactor();
    const float glyphSize = 8.0f * textScale; // BitmapFont glyph is 8px
    const float charAdvance = glyphSize * advanceFactor;
    const float textWidth = static_cast<float>(m_itemName.size()) * charAdvance;
    const float textX = hotbarCenterX - textWidth * 0.5f;

    textRenderer.render(m_itemName, textX, textY, textScale, textColor, screenW, screenH);
}

void HotbarControl::setItemNameDisplayDuration(float seconds)
{
    m_itemNameDisplayDuration = std::clamp(seconds, 0.5f, 10.0f);
}

float HotbarControl::getItemNameDisplayDuration() const
{
    return m_itemNameDisplayDuration;
}


