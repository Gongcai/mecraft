#include "HotbarControl.h"

#include <glad/glad.h>

#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../player/Inventory.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/core/Shader.h"
#include "../../world/block/Block.h"
#include "../../item/Item.h"
#include "../font/TextRenderer.h"
#include "../layout/UILayout.h"
#include "../../locale/LocaleManager.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderUtils.h"
#include "../core/UITheme.h"

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

void HotbarControl::renderSelf(const UIRenderContext& context) const
{
    const Inventory* inventory = context.inventory ? context.inventory : m_inventory;
    if (!inventory || context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    // Apply theme colors if available
    const UITheme* theme = context.theme;
    auto savedBg = m_bgColor;
    auto savedBorder = m_borderColor;
    auto savedIcon = m_iconTintColor;
    if (theme) {
        const_cast<HotbarControl*>(this)->m_bgColor = theme->hotbarBackground;
        const_cast<HotbarControl*>(this)->m_borderColor = theme->hotbarBorder;
        const_cast<HotbarControl*>(this)->m_iconTintColor = theme->hotbarIconTint;
    }

    renderInternal(static_cast<float>(context.screenWidth),
                   static_cast<float>(context.screenHeight),
                   *inventory,
                   context.textRenderer);

    // Restore original colors
    if (theme) {
        const_cast<HotbarControl*>(this)->m_bgColor = savedBg;
        const_cast<HotbarControl*>(this)->m_borderColor = savedBorder;
        const_cast<HotbarControl*>(this)->m_iconTintColor = savedIcon;
    }

    // Render item name popup above hotbar
    if (context.textRenderer) {
        checkSlotChange(*inventory, context.localeManager);
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

void HotbarControl::setCountTextScale(float scale)
{
    m_countTextScale = std::max(0.1f, scale);
}

float HotbarControl::getCountTextScale() const
{
    return m_countTextScale;
}

void HotbarControl::renderInternal(float screenW, float screenH, const Inventory& inventory, const TextRenderer* textRenderer) const
{
    if (!m_inventoryShader || !m_resourceMgr || m_vao == 0 || m_vbo == 0) {
        return;
    }

    constexpr int hotbarSlots = Inventory::HOTBAR_SIZE;
    constexpr float slotStride = 20.0f * HotbarLayout::kScale;

    // Check if inventory state changed since last build
    const int currentSelected = inventory.getSelectedSlot();
    bool stateChanged = m_dirty || m_cachedSelectedSlot != currentSelected ||
                        m_cachedScreenW != screenW || m_cachedScreenH != screenH;
    if (!stateChanged) {
        for (int i = 0; i < kHotbarSlots; ++i) {
            const ItemStack stack = inventory.getSlotStack(i);
            if (m_cachedSlotCounts[i] != static_cast<int>(stack.count) ||
                m_cachedSlotItems[i] != stack.itemId) {
                stateChanged = true;
                break;
            }
        }
    }

    if (!stateChanged && m_cachedVertCount > 0) {
        // Nothing changed — replay cached draw sequence
        const UIRenderUtils::GLStateGuard glState;
        m_inventoryShader->use();
        m_inventoryShader->setVec2("uScreenSize", glm::vec2(screenW, screenH));
        glActiveTexture(GL_TEXTURE0);
        m_inventoryShader->setInt("uAtlas", 0);
        glBindVertexArray(m_vao);

        int offset = 0;
        // Background
        glBindTexture(GL_TEXTURE_2D, m_cachedBgTexture);
        m_inventoryShader->setVec4("uTintColor", glm::vec4(m_bgColor[0], m_bgColor[1], m_bgColor[2], m_bgColor[3]));
        glDrawArrays(GL_TRIANGLES, offset, m_cachedBgVertCount);
        offset += m_cachedBgVertCount;
        // Selected highlight
        m_inventoryShader->setVec4("uTintColor", glm::vec4(m_borderColor[0], m_borderColor[1], m_borderColor[2], m_borderColor[3]));
        glDrawArrays(GL_TRIANGLES, offset, m_cachedSelectedVertCount);
        offset += m_cachedSelectedVertCount;
        // Item icons (up to 3 batches: itemTexture, itemIcon, legacy)
        m_inventoryShader->setVec4("uTintColor", glm::vec4(m_iconTintColor[0], m_iconTintColor[1], m_iconTintColor[2], m_iconTintColor[3]));
        for (int b = 0; b < 3; ++b) {
            if (m_cachedIconVertCounts[b] > 0) {
                glBindTexture(GL_TEXTURE_2D, m_cachedIconTextures[b]);
                glDrawArrays(GL_TRIANGLES, offset, m_cachedIconVertCounts[b]);
                offset += m_cachedIconVertCounts[b];
            }
        }

        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Render item count text using cached slot data
        if (textRenderer) {
            renderCountText(screenW, screenH, m_cachedSlotCounts, hotbarSlots, slotStride,
                            m_cachedStartX, m_cachedStartY, *textRenderer);
        }
        return;
    }

    // ── Rebuild vertices ──
    m_cachedSelectedSlot = currentSelected;
    m_cachedScreenW = screenW;
    m_cachedScreenH = screenH;
    for (int i = 0; i < kHotbarSlots; ++i) {
        const ItemStack stack = inventory.getSlotStack(i);
        m_cachedSlotCounts[i] = static_cast<int>(stack.count);
        m_cachedSlotItems[i] = stack.itemId;
    }
    m_dirty = false;

    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resourceMgr->getItemTextureAtlas();
    const uint32_t widgetsTexture = m_resourceMgr->getGuiTexture("widgets");
    if (widgetsTexture == 0) {
        return;
    }

    const bool hasItemTextures = (itemTextureAtlas.textureID != 0 && itemTextureAtlas.tilesPerRow > 0);
    const bool hasBakedItemIcons = (itemIconAtlas.textureID != 0 && itemIconAtlas.tilesPerRow > 0);
    const bool hasLegacyAtlas = (atlas.textureID != 0 && atlas.tilesPerRow > 0);
    if (!hasItemTextures && !hasBakedItemIcons && !hasLegacyAtlas) {
        return;
    }

    const float hotbarWidth = HotbarLayout::kWidth;
    const float hotbarHeight = HotbarLayout::kHeight;

    const UILayout layout{Anchor::BottomCenter, 0.0f, HotbarLayout::kBottomMargin};
    const float startX = layout.resolveX(screenW, hotbarWidth);
    const float startY = layout.resolveY(screenH, hotbarHeight);

    // Cache layout position for text rendering
    m_cachedStartX = startX;
    m_cachedStartY = startY;

    const int selectedSlot = std::clamp(inventory.getSelectedSlot(), 0, hotbarSlots - 1);

    auto uvFromTopLeftPixels = [](float x0, float y0, float x1, float y1)
    {
        const float u0 = x0 / HotbarLayout::kWidgetsWidth;
        const float u1 = x1 / HotbarLayout::kWidgetsWidth;
        const float v0 = (HotbarLayout::kWidgetsHeight - y1) / HotbarLayout::kWidgetsHeight;
        const float v1 = (HotbarLayout::kWidgetsHeight - y0) / HotbarLayout::kWidgetsHeight;
        return std::array<float, 4>{u0, v0, u1, v1};
    };

    std::vector<float> bgVerts;
    {
        const auto uv = uvFromTopLeftPixels(1.0f, 0.0f, 182.0f, 21.0f);
        UIRenderUtils::pushTexturedQuad(bgVerts,
                startX, startY,
                startX + hotbarWidth, startY + hotbarHeight,
                uv[0], uv[1], uv[2], uv[3]);
    }

    std::vector<float> selectedVerts;
    {
        const auto uv = uvFromTopLeftPixels(0.0f, 21.0f, 25.0f, 46.0f);
        const float selectorOffset = ((HotbarLayout::kHighlightSize - 20.0f) * 0.5f) * HotbarLayout::kScale;
        const float selX = startX + static_cast<float>(selectedSlot) * slotStride - selectorOffset + 2;
        const float selY = startY - 3.0f;
        UIRenderUtils::pushTexturedQuad(selectedVerts,
                selX, selY,
                selX + HotbarLayout::kHighlightSize * HotbarLayout::kScale, selY + HotbarLayout::kHighlightSize * HotbarLayout::kScale,
                uv[0], uv[1], uv[2], uv[3]);
    }

    std::vector<float> iconVerts;
    std::vector<float> fallbackIconVerts;
    std::vector<float> legacyIconVerts;
    constexpr float iconInset = 2.0f * HotbarLayout::kScale;
    constexpr float iconSize = 17.5f * HotbarLayout::kScale;
    for (int i = 0; i < hotbarSlots; ++i)
    {
        const ItemStack stack = inventory.getSlotStack(i);
        const ItemID itemId = stack.itemId;
        if (itemId == 0) {
            continue;
        }

        const ItemDef& itemDef = ItemRegistry::get(itemId);
        glm::vec2 uvMin;
        glm::vec2 uvMax;
        std::vector<float>* targetBuffer = nullptr;
        const bool useBakedBlockIcon = hasBakedItemIcons && ui::shouldUseBakedBlockIcon(itemDef);
        if (useBakedBlockIcon) {
            const auto uv = itemIconAtlas.getUV(static_cast<int>(itemDef.renderBlock));
            uvMin = uv.first;
            uvMax = uv.second;
            targetBuffer = &fallbackIconVerts;
        }

        if (!targetBuffer && hasItemTextures) {
            const int itemTileIndex = m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
            if (itemTileIndex >= 0) {
                const auto uv = itemTextureAtlas.getUV(itemTileIndex);
                uvMin = uv.first;
                uvMax = uv.second;
                targetBuffer = &iconVerts;
            }
        }

        if (!targetBuffer && hasBakedItemIcons) {
            const auto uv = itemIconAtlas.getUV(static_cast<int>(itemDef.renderBlock));
            uvMin = uv.first;
            uvMax = uv.second;
            targetBuffer = &fallbackIconVerts;
        }

        if (!targetBuffer && hasLegacyAtlas) {
            const BlockID renderBlock = itemDef.renderBlock;
            if (renderBlock == 0) {
                continue;
            }
            const BlockDef& blockDef = BlockRegistry::get(renderBlock);
            int tileIndex = blockDef.faceFront.firstLayer;
            if (tileIndex < 0) {
                tileIndex = blockDef.faceTop.firstLayer;
            }
            if (tileIndex < 0) {
                continue;
            }

            const auto uv = atlas.getUV(tileIndex);
            uvMin = uv.first;
            uvMax = uv.second;
            targetBuffer = &legacyIconVerts;
        }

        if (!targetBuffer) {
            continue;
        }

        const float sx = startX + static_cast<float>(i) * slotStride;
        const float ix = sx + iconInset;
        const float iy = startY + iconInset;

        UIRenderUtils::pushTexturedQuad(*targetBuffer, ix, iy, ix + iconSize, iy + iconSize, uvMin.x, uvMin.y, uvMax.x, uvMax.y);
    }

    std::vector<float> vertices;
    vertices.reserve(bgVerts.size() + selectedVerts.size() + iconVerts.size() + fallbackIconVerts.size() + legacyIconVerts.size());
    vertices.insert(vertices.end(), bgVerts.begin(), bgVerts.end());
    vertices.insert(vertices.end(), selectedVerts.begin(), selectedVerts.end());
    vertices.insert(vertices.end(), iconVerts.begin(), iconVerts.end());
    vertices.insert(vertices.end(), fallbackIconVerts.begin(), fallbackIconVerts.end());
    vertices.insert(vertices.end(), legacyIconVerts.begin(), legacyIconVerts.end());

    const int bgVertCount = static_cast<int>(bgVerts.size() / 4);
    const int selectedVertCount = static_cast<int>(selectedVerts.size() / 4);
    const int iconVertCount = static_cast<int>(iconVerts.size() / 4);
    const int fallbackIconVertCount = static_cast<int>(fallbackIconVerts.size() / 4);
    const int legacyIconVertCount = static_cast<int>(legacyIconVerts.size() / 4);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    {
        const UIRenderUtils::GLStateGuard glState;

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

        m_inventoryShader->setVec4("uTintColor", glm::vec4(m_iconTintColor[0], m_iconTintColor[1], m_iconTintColor[2], m_iconTintColor[3]));
        if (iconVertCount > 0) {
            glBindTexture(GL_TEXTURE_2D, itemTextureAtlas.textureID);
            glDrawArrays(GL_TRIANGLES, offset, iconVertCount);
        }
        offset += iconVertCount;
        if (fallbackIconVertCount > 0) {
            glBindTexture(GL_TEXTURE_2D, itemIconAtlas.textureID);
            glDrawArrays(GL_TRIANGLES, offset, fallbackIconVertCount);
        }
        offset += fallbackIconVertCount;
        if (legacyIconVertCount > 0) {
            glBindTexture(GL_TEXTURE_2D, atlas.textureID);
            glDrawArrays(GL_TRIANGLES, offset, legacyIconVertCount);
        }

        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Cache draw state for next frame
        m_cachedBgVertCount = bgVertCount;
        m_cachedSelectedVertCount = selectedVertCount;
        m_cachedBgTexture = widgetsTexture;
        m_cachedIconVertCounts[0] = iconVertCount;
        m_cachedIconVertCounts[1] = fallbackIconVertCount;
        m_cachedIconVertCounts[2] = legacyIconVertCount;
        m_cachedIconTextures[0] = itemTextureAtlas.textureID;
        m_cachedIconTextures[1] = itemIconAtlas.textureID;
        m_cachedIconTextures[2] = atlas.textureID;
        m_cachedVertCount = bgVertCount + selectedVertCount + iconVertCount + fallbackIconVertCount + legacyIconVertCount;
    }

    // ── Render item count text ──
    if (textRenderer) {
        renderCountText(screenW, screenH, m_cachedSlotCounts, hotbarSlots, slotStride,
                        startX, startY, *textRenderer);
    }
}

void HotbarControl::renderCountText(float screenW, float screenH, const int* slotCounts, int slotCount,
                                     float slotStride, float startX, float startY, const TextRenderer& textRenderer) const
{
    constexpr float kBaseGlyphSize = 8.0f;
    constexpr float kCountRightPaddingRatio = 0.05f;
    constexpr float kCountBottomPaddingRatio = 0.03f;
    constexpr std::array<float, 4> kTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
    const float slotFullSize = slotStride;
    const float textScale = m_countTextScale * slotFullSize / kBaseGlyphSize;

    textRenderer.beginBatch(screenW, screenH);

    for (int i = 0; i < slotCount; ++i)
    {
        if (slotCounts[i] <= 1)
            continue;

        const std::string countStr = std::to_string(slotCounts[i]);
        const float textWidth = textRenderer.measureText(countStr, textScale).width;
        const float slotX = startX + static_cast<float>(i) * slotStride;
        const float textRightX = slotX + slotFullSize - kCountRightPaddingRatio * slotFullSize;
        const float textX = textRightX - textWidth;
        const float textY = startY + kCountBottomPaddingRatio * slotFullSize;

        textRenderer.batchRender(countStr, textX, textY, textScale, kTextColor);
    }

    textRenderer.endBatch();
}

void HotbarControl::checkSlotChange(const Inventory& inventory, const LocaleManager* localeManager) const
{
    const int currentSlot = inventory.getSelectedSlot();
    const ItemID currentItem = inventory.getSelectedItem();

    if (m_lastSelectedSlot < 0) {
        m_lastSelectedSlot = currentSlot;
        m_lastSelectedItem = currentItem;
        return;
    }

    if (currentSlot != m_lastSelectedSlot || currentItem != m_lastSelectedItem) {
        m_lastSelectedSlot = currentSlot;
        m_lastSelectedItem = currentItem;
        if (currentItem != 0) {
            if (localeManager) {
                m_itemName = localeManager->getItemName(ItemRegistry::get(currentItem).namespacedId.path());
            } else {
                m_itemName = std::string(ItemRegistry::get(currentItem).namespacedId.path());
            }
        } else {
            m_itemName.clear();
        }
        m_itemNameShowTime = 0.0f;
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
    const float hotbarCenterX = screenW * 0.5f;

    const float textScale = 1.5f;
    const float textY = HotbarLayout::kBottomMargin + HotbarLayout::kHeight + 6.0f;

    const std::array<float, 4> textColor = {1.0f, 1.0f, 1.0f, alpha};

    // Estimate text width for centering
    const float textWidth = textRenderer.measureText(m_itemName, textScale).width;
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
