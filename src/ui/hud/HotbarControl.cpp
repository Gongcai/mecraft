#include "HotbarControl.h"

#include <algorithm>
#include <cmath>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../player/Inventory.h"
#include "../../resource/GameResources.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../world/block/Block.h"
#include "../../item/Item.h"
#include "../font/TextRenderer.h"
#include "../layout/UILayout.h"
#include "../../locale/LocaleManager.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderer.h"
#include "../core/UITheme.h"

namespace {

struct HotbarImagePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

static_assert(sizeof(HotbarImagePushConstants) == 64u);

[[nodiscard]] RhiRect2D hotbarScissor(const UIRenderContext& context) {
    if (context.hasScissor) {
        return context.scissor;
    }
    return {0, 0, static_cast<uint32_t>(std::max(1.0f, std::round(context.screenWidth * context.pixelScale()))),
            static_cast<uint32_t>(std::max(1.0f, std::round(context.screenHeight * context.pixelScale())))};
}

} // namespace

void HotbarControl::init(GameResources& resources, RhiDevice& rhiDevice) {
    UIWidget::init(resources, rhiDevice);
    m_resources = &resources;
}

void HotbarControl::shutdown() {
    m_resources = nullptr;
    m_inventory = nullptr;
    UIWidget::shutdown();
}

void HotbarControl::setInventorySource(const Inventory* inventory) {
    m_inventory = inventory;
}

void HotbarControl::renderSelf(const UIRenderContext& context) const {
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

    renderInternal(context, *inventory);

    // Restore original colors
    if (theme) {
        const_cast<HotbarControl*>(this)->m_bgColor = savedBg;
        const_cast<HotbarControl*>(this)->m_borderColor = savedBorder;
        const_cast<HotbarControl*>(this)->m_iconTintColor = savedIcon;
    }

    // Render item name popup above hotbar
    if (context.textRenderer) {
        checkSlotChange(*inventory, context.localeManager);
        renderItemName(context, *inventory, *context.textRenderer, context.timeSeconds);
    }
}

void HotbarControl::setBgColor(const std::array<float, 4>& color) {
    m_bgColor = color;
}

const std::array<float, 4>& HotbarControl::getBgColor() const {
    return m_bgColor;
}

void HotbarControl::setBorderColor(const std::array<float, 4>& color) {
    m_borderColor = color;
}

const std::array<float, 4>& HotbarControl::getBorderColor() const {
    return m_borderColor;
}

void HotbarControl::setIconTintColor(const std::array<float, 4>& color) {
    m_iconTintColor = color;
}

const std::array<float, 4>& HotbarControl::getIconTintColor() const {
    return m_iconTintColor;
}

void HotbarControl::setCountTextScale(float scale) {
    m_countTextScale = std::max(0.1f, scale);
}

float HotbarControl::getCountTextScale() const {
    return m_countTextScale;
}

void HotbarControl::renderInternal(const UIRenderContext& context, const Inventory& inventory) const {
    const float screenW = static_cast<float>(context.screenWidth);
    const float screenH = static_cast<float>(context.screenHeight);
    const TextRenderer* textRenderer = context.textRenderer;
    if (!m_resources) {
        return;
    }

    constexpr int hotbarSlots = Inventory::HOTBAR_SIZE;
    constexpr float slotStride = 20.0f * HotbarLayout::kScale;

    for (int i = 0; i < kHotbarSlots; ++i) {
        const ItemStack stack = inventory.getSlotStack(i);
        m_cachedSlotCounts[i] = static_cast<int>(stack.count);
    }

    const TextureAtlas& itemIconAtlas = m_resources->uiTextures.blockIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resources->uiTextures.itemTextureAtlas();
    const RhiTextureHandle widgetsTexture = m_resources->texture2D.getGuiHandle("widgets");
    if (!widgetsTexture.isValid()) {
        return;
    }

    const float hotbarWidth = HotbarLayout::kWidth;
    const float hotbarHeight = HotbarLayout::kHeight;

    const UILayout layout{Anchor::BottomCenter, 0.0f, HotbarLayout::kBottomMargin};
    const float startX = layout.resolveX(screenW, hotbarWidth);
    const float startY = layout.resolveY(screenH, hotbarHeight);

    if (context.phase == UIRenderPhase::CollectText) {
        if (textRenderer != nullptr) {
            renderCountText(context, m_cachedSlotCounts, hotbarSlots, slotStride, startX, startY, *textRenderer);
        }
        return;
    }

    if (context.commandList == nullptr || context.uiRenderer == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid()) {
        return;
    }

    const int selectedSlot = std::clamp(inventory.getSelectedSlot(), 0, hotbarSlots - 1);

    RhiCommandList& commandList = *context.commandList;
    commandList.setGraphicsPipeline(context.imageTexturePipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.setScissor(hotbarScissor(context));

    const auto drawImage = [&](const RhiTextureHandle texture, const float x, const float y, const float width,
                               const float height, const std::array<float, 4>& uv, const std::array<float, 4>& tint) {
        const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(texture);
        if (!bindGroup.isValid()) {
            return;
        }
        const HotbarImagePushConstants pushConstants{
            glm::vec4(screenW, screenH, x, y), glm::vec4(width, height, 0.0f, 0.0f),
            glm::vec4(uv[0], uv[1], uv[2], uv[3]), glm::vec4(tint[0], tint[1], tint[2], tint[3])};
        commandList.setBindGroup(0u, bindGroup);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
    };

    auto uvFromTopLeftPixels = [](float x0, float y0, float x1, float y1) {
        const float u0 = x0 / HotbarLayout::kWidgetsWidth;
        const float u1 = x1 / HotbarLayout::kWidgetsWidth;
        const float v0 = (HotbarLayout::kWidgetsHeight - y1) / HotbarLayout::kWidgetsHeight;
        const float v1 = (HotbarLayout::kWidgetsHeight - y0) / HotbarLayout::kWidgetsHeight;
        return std::array<float, 4>{u0, v0, u1, v1};
    };

    {
        const auto uv = uvFromTopLeftPixels(1.0f, 0.0f, 182.0f, 21.0f);
        drawImage(widgetsTexture, startX, startY, hotbarWidth, hotbarHeight, uv, m_bgColor);
    }

    {
        const auto uv = uvFromTopLeftPixels(0.0f, 21.0f, 25.0f, 46.0f);
        const float selectorOffset = ((HotbarLayout::kHighlightSize - 20.0f) * 0.5f) * HotbarLayout::kScale;
        const float selX = startX + static_cast<float>(selectedSlot) * slotStride - selectorOffset + 2;
        const float selY = startY - 3.0f;
        const float selectorSize = HotbarLayout::kHighlightSize * HotbarLayout::kScale;
        drawImage(widgetsTexture, selX, selY, selectorSize, selectorSize, uv, m_borderColor);
    }

    constexpr float iconInset = 2.0f * HotbarLayout::kScale;
    constexpr float iconSize = 17.5f * HotbarLayout::kScale;
    for (int i = 0; i < hotbarSlots; ++i) {
        const ItemStack stack = inventory.getSlotStack(i);
        const ItemID itemId = stack.itemId;
        if (itemId == 0) {
            continue;
        }

        const ItemDef& itemDef = ItemRegistry::get(itemId);
        RhiTextureHandle iconTexture;
        std::pair<glm::vec2, glm::vec2> iconUv;
        if (ui::shouldUseBakedBlockIcon(itemDef)) {
            if (!itemIconAtlas.texture.isValid() || itemIconAtlas.tilesPerRow <= 0) {
                continue;
            }
            iconTexture = itemIconAtlas.texture;
            iconUv = itemIconAtlas.getUV(static_cast<int>(itemDef.renderBlock));
        } else {
            if (!itemTextureAtlas.texture.isValid() || itemTextureAtlas.tilesPerRow <= 0) {
                continue;
            }
            const int itemTileIndex = m_resources->uiTextures.itemTextureIndex(itemDef.iconTextureName);
            if (itemTileIndex < 0) {
                continue;
            }
            iconTexture = itemTextureAtlas.texture;
            iconUv = itemTextureAtlas.getUV(itemTileIndex);
        }

        const float sx = startX + static_cast<float>(i) * slotStride;
        const float ix = sx + iconInset;
        const float iy = startY + iconInset;

        drawImage(iconTexture, ix, iy, iconSize, iconSize,
                  {iconUv.first.x, iconUv.first.y, iconUv.second.x, iconUv.second.y}, m_iconTintColor);
    }

    // ── Render item count text ──
    if (textRenderer) {
        renderCountText(context, m_cachedSlotCounts, hotbarSlots, slotStride, startX, startY, *textRenderer);
    }
}

void HotbarControl::renderCountText(const UIRenderContext& context, const int* slotCounts, int slotCount,
                                    float slotStride, float startX, float startY,
                                    const TextRenderer& textRenderer) const {
    constexpr float kBaseGlyphSize = 8.0f;
    constexpr float kCountRightPaddingRatio = 0.05f;
    constexpr float kCountBottomPaddingRatio = 0.03f;
    constexpr std::array<float, 4> kTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
    const float slotFullSize = slotStride;
    const float textScale = m_countTextScale * slotFullSize / kBaseGlyphSize;

    for (int i = 0; i < slotCount; ++i) {
        if (slotCounts[i] <= 1)
            continue;

        const std::string countStr = std::to_string(slotCounts[i]);
        const float textWidth = textRenderer.measureText(countStr, textScale).width;
        const float slotX = startX + static_cast<float>(i) * slotStride;
        const float textRightX = slotX + slotFullSize - kCountRightPaddingRatio * slotFullSize;
        const float textX = textRightX - textWidth;
        const float textY = startY + kCountBottomPaddingRatio * slotFullSize;

        textRenderer.draw(context, countStr, textX, textY, textScale, kTextColor);
    }
}

void HotbarControl::checkSlotChange(const Inventory& inventory, const LocaleManager* localeManager) const {
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

void HotbarControl::renderItemName(const UIRenderContext& context, const Inventory& inventory,
                                   const TextRenderer& textRenderer, float timeSeconds) const {
    (void)inventory;
    const float screenW = static_cast<float>(context.screenWidth);
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

    textRenderer.draw(context, m_itemName, textX, textY, textScale, textColor);
}

void HotbarControl::setItemNameDisplayDuration(float seconds) {
    m_itemNameDisplayDuration = std::clamp(seconds, 0.5f, 10.0f);
}

float HotbarControl::getItemNameDisplayDuration() const {
    return m_itemNameDisplayDuration;
}
