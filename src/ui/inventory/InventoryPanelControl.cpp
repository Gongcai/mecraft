#include "InventoryPanelControl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../crafting/CraftingSystem.h"
#include "../../item/Item.h"
#include "../../player/Inventory.h"
#include "../../renderer/renderers/HumanoidRenderer.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/GameResources.h"
#include "../../locale/LocaleManager.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderer.h"

namespace {
struct InventoryImagePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

static_assert(sizeof(InventoryImagePushConstants) == 64u);

[[nodiscard]] RhiRect2D inventoryScissor(const UIRenderContext& context) {
    return context.fullFramebufferScissor();
}

void drawTexturedQuad(const UIRenderContext& context, const RhiTextureHandle texture, const float x, const float y,
                      const float width, const float height, const glm::vec4& uvRect, const glm::vec4& tint) {
    if (context.commandList == nullptr || context.uiRenderer == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid() || !texture.isValid() || context.uiWidth <= 0 ||
        context.uiHeight <= 0 || width <= 0.0f || height <= 0.0f) {
        return;
    }

    const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(texture);
    if (!bindGroup.isValid()) {
        return;
    }

    const InventoryImagePushConstants pushConstants{
        glm::vec4(static_cast<float>(context.uiWidth), static_cast<float>(context.uiHeight), x, y),
        glm::vec4(width, height, 0.0f, 0.0f), uvRect, tint};

    RhiCommandList& commandList = *context.commandList;
    commandList.setGraphicsPipeline(context.imageTexturePipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.setBindGroup(0u, bindGroup);
    commandList.setScissor(inventoryScissor(context));
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(6u, 1u, 0u, 0u);
}
} // namespace

void InventoryPanelControl::init(GameResources& resources, RhiDevice& rhiDevice) {
    UIWidget::init(resources, rhiDevice);
    m_resources = &resources;

    m_itemGrid.init(resources, rhiDevice);
    m_craftingGrid.init(resources, rhiDevice);
    m_tooltip.init(resources, rhiDevice);
}

void InventoryPanelControl::shutdown() {
    m_tooltip.shutdown();
    m_craftingGrid.shutdown();
    m_itemGrid.shutdown();
    m_inventory = nullptr;
    m_craftingSystem = nullptr;
    m_resources = nullptr;
    UIWidget::shutdown();
}

void InventoryPanelControl::renderSelf(const UIRenderContext& context) const {
    auto* self = const_cast<InventoryPanelControl*>(this);
    self->m_cachedUiWidth = context.uiWidth;
    self->m_cachedUiHeight = context.uiHeight;
    self->syncSlotsFromInventory();

    const ResolvedPanelRect panelRect = resolvePanelRect(context.uiWidth, context.uiHeight);
    self->syncCraftingGridPosition(panelRect);

    // Update crafting result when visible
    if (m_craftingSystem) {
        self->m_craftingGrid.updateCraftingResult(*m_craftingSystem);
    }

    renderBackground(context);
    renderPlayerPreview(context, panelRect);
    m_craftingGrid.render(context);
    m_itemGrid.render(context);
    renderDraggedItem(context);

    // Tooltip: show item name on hover, hide when dragging
    if (context.hasDraggedItem) {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    } else {
        ItemID hoveredId = m_itemGrid.getHoveredItemId();
        if (hoveredId == 0) {
            // Check crafting grid hover
            const int craftSlot = m_craftingGrid.getHoveredSlot();
            if (craftSlot >= 0 && craftSlot < m_craftingGrid.getCraftingCellCount()) {
                hoveredId = m_craftingGrid.getCraftingSlot(craftSlot);
            }
        }

        if (hoveredId != 0) {
            const ItemDef& def = ItemRegistry::get(hoveredId);
            const std::string name = context.localeManager ? context.localeManager->getItemName(def.namespacedId.path())
                                                           : std::string(def.namespacedId.path());
            if (hoveredId != m_tooltipHoveredItemId) {
                m_tooltipHoveredItemId = hoveredId;
                m_tooltip.startHover(name, context.pointerX, context.pointerY, static_cast<float>(context.uiWidth),
                                     static_cast<float>(context.uiHeight), context.timeSeconds);
            } else if (m_tooltip.isHovering()) {
                m_tooltip.startHover(name, context.pointerX, context.pointerY, static_cast<float>(context.uiWidth),
                                     static_cast<float>(context.uiHeight), context.timeSeconds);
            }
        } else {
            m_tooltip.cancelHover();
            m_tooltipHoveredItemId = 0;
        }
    }
    m_tooltip.render(context);
}

UIEventResult InventoryPanelControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) {
        return UIEventResult::Ignored;
    }
    syncSlotsFromInventory();

    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedUiWidth, m_cachedUiHeight);
    syncCraftingGridPosition(panelRect);

    // Crafting grid gets input priority (rendered on top)
    UIEventResult result = m_craftingGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        // Clear inventory grid's activation to avoid stale state
        m_itemGrid.clearLastActivatedIndex();
        return result;
    }
    if (result == UIEventResult::Handled) {
        return result;
    }

    result = m_itemGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        // Clear crafting grid's activation to avoid stale state
        m_craftingGrid.clearActivation();
    }
    return result;
}

void InventoryPanelControl::setVisible(bool isVisible) {
    visible = isVisible;
    m_itemGrid.setVisible(isVisible);
    m_craftingGrid.setVisible(isVisible);
}

void InventoryPanelControl::setSlots(const Pickable::SlotInfo* slots, int count) {
    m_useExternalSlots = true;
    m_itemGrid.setSlots(slots, count);
}

void InventoryPanelControl::setInventorySource(const Inventory* inventory) {
    m_inventory = inventory;
    m_useExternalSlots = false;
}

void InventoryPanelControl::setLayout(const InventoryPanelLayout& layout) {
    m_layout = layout;
    m_craftingGrid.setLayout(layout.craftingGrid);
    syncSlotsFromInventory();
}

const InventoryPanelLayout& InventoryPanelControl::getLayout() const {
    return m_layout;
}

ItemGridControl& InventoryPanelControl::itemGrid() {
    return m_itemGrid;
}

const ItemGridControl& InventoryPanelControl::itemGrid() const {
    return m_itemGrid;
}

CraftingGridControl& InventoryPanelControl::craftingGrid() {
    return m_craftingGrid;
}

const CraftingGridControl& InventoryPanelControl::craftingGrid() const {
    return m_craftingGrid;
}

void InventoryPanelControl::setCraftingSystem(const CraftingSystem* craftingSystem) {
    m_craftingSystem = craftingSystem;
}

void InventoryPanelControl::syncSlotsFromInventory() {
    if (m_useExternalSlots) {
        return;
    }
    if (!m_inventory) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedUiWidth, m_cachedUiHeight);
    const float scale = panelRect.scale;

    std::array<Pickable::SlotInfo, Inventory::INVENTORY_SIZE> slots{};
    const int baseX = static_cast<int>(std::lround(panelRect.x + m_layout.gridOffsetX * scale));
    const int slotSize = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * scale)));
    const int baseY =
        static_cast<int>(std::lround(panelRect.y + panelRect.height - m_layout.gridOffsetY * scale)) - slotSize;
    const int colStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.columnGap) * scale)));
    const int rowStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.rowGap) * scale)));
    const int extraRow4 = static_cast<int>(std::lround(m_layout.row4ExtraGap * scale));

    int outIndex = 0;
    for (int row = 0; row < Inventory::INVENTORY_ROWS; ++row) {
        const int slotY = baseY - row * rowStep - (row >= 3 ? extraRow4 : 0);
        for (int col = 0; col < Inventory::INVENTORY_COLUMNS; ++col) {
            const int inventoryIndex = Inventory::toInventoryIndex(row, col);
            const ItemStack stack = m_inventory->getSlotStack(inventoryIndex);
            slots[static_cast<size_t>(outIndex)] = {baseX + col * colStep, slotY, slotSize,
                                                    static_cast<int>(stack.itemId), static_cast<int>(stack.count)};
            ++outIndex;
        }
    }

    m_itemGrid.setSlots(slots.data(), static_cast<int>(slots.size()));
}

void InventoryPanelControl::syncCraftingGridPosition(const ResolvedPanelRect& panelRect) {
    m_craftingGrid.setPanelOrigin(panelRect.x, panelRect.y, panelRect.height, panelRect.scale);
    m_craftingGrid.setLayout(m_layout.craftingGrid);
}

void InventoryPanelControl::renderBackground(const UIRenderContext& context) const {
    if (!m_resources || context.uiWidth <= 0 || context.uiHeight <= 0 ||
        m_layout.backgroundAtlasWidth <= 0.0f || m_layout.backgroundAtlasHeight <= 0.0f) {
        return;
    }

    const RhiTextureHandle texture = m_resources->texture2D.getGuiHandle(m_layout.backgroundTextureName);
    if (!texture.isValid()) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.uiWidth, context.uiHeight);
    const float atlasWidth = m_layout.backgroundAtlasWidth;
    const float atlasHeight = m_layout.backgroundAtlasHeight;
    const float u0 = 0.0f;
    const float u1 = InventoryPanelLayout::kTextureWidth / atlasWidth;
    const float v0 = 1.0f - InventoryPanelLayout::kTextureHeight / atlasHeight;
    const float v1 = 1.0f;
    drawTexturedQuad(context, texture, panelRect.x, panelRect.y, panelRect.width, panelRect.height,
                     glm::vec4(u0, v0, u1, v1), glm::vec4(1.0f));
}

void InventoryPanelControl::renderPlayerPreview(const UIRenderContext& context,
                                                const ResolvedPanelRect& panelRect) const {
    if (!context.humanoidRenderer || context.commandList == nullptr || context.uiToFramebufferScaleX() <= 0.0f ||
        context.uiToFramebufferScaleY() <= 0.0f) {
        return;
    }
    if (!m_layout.showPlayerPreview) {
        return;
    }

    const float previewWidth = std::max(1.0f, (m_layout.playerPreviewX1 - m_layout.playerPreviewX0) * panelRect.scale);
    const float previewHeight = std::max(1.0f, (m_layout.playerPreviewY1 - m_layout.playerPreviewY0) * panelRect.scale);
    const float previewX = panelRect.x + m_layout.playerPreviewX0 * panelRect.scale;
    const float previewY = panelRect.y + panelRect.height - m_layout.playerPreviewY1 * panelRect.scale;

    context.humanoidRenderer->renderInventoryPreview(
        *context.commandList, previewX, previewY, previewWidth, previewHeight, context.uiToFramebufferScaleX(),
        context.uiToFramebufferScaleY(), context.pointerX, context.pointerY, context.timeSeconds,
        context.framebufferWidth, context.framebufferHeight);
}

void InventoryPanelControl::renderDraggedItem(const UIRenderContext& context) const {
    if (!context.hasDraggedItem || context.draggedItemId <= 0 || !m_resources) {
        return;
    }

    const TextureAtlas& itemIconAtlas = m_resources->uiTextures.blockIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resources->uiTextures.itemTextureAtlas();

    const ResolvedPanelRect panelRect = resolvePanelRect(context.uiWidth, context.uiHeight);
    const float iconSize = std::max(1.0f, m_layout.slotSize * panelRect.scale);
    constexpr float kDragCursorOffsetPx = 1.0f;
    const float x0 = context.pointerX + kDragCursorOffsetPx;
    const float y0 = context.pointerY - iconSize - kDragCursorOffsetPx;

    const auto draggedItem = static_cast<ItemID>(context.draggedItemId);
    const ItemDef& itemDef = ItemRegistry::get(draggedItem);
    RhiTextureHandle texture;
    std::pair<glm::vec2, glm::vec2> uv;
    if (ui::shouldUseBakedBlockIcon(itemDef)) {
        if (!itemIconAtlas.texture.isValid() || itemIconAtlas.tilesPerRow <= 0) {
            return;
        }
        texture = itemIconAtlas.texture;
        uv = itemIconAtlas.getUV(static_cast<int>(itemDef.renderBlock));
    } else {
        if (!itemTextureAtlas.texture.isValid() || itemTextureAtlas.tilesPerRow <= 0) {
            return;
        }
        const int itemTileIndex = m_resources->uiTextures.itemTextureIndex(itemDef.iconTextureName);
        if (itemTileIndex < 0) {
            return;
        }
        texture = itemTextureAtlas.texture;
        uv = itemTextureAtlas.getUV(itemTileIndex);
    }

    drawTexturedQuad(context, texture, x0, y0, iconSize, iconSize,
                     glm::vec4(uv.first.x, uv.first.y, uv.second.x, uv.second.y), glm::vec4(1.0f, 1.0f, 1.0f, 0.95f));
}

InventoryPanelControl::ResolvedPanelRect InventoryPanelControl::resolvePanelRect(const int uiWidth,
                                                                                 const int uiHeight) const {
    const int safeWidth = std::max(1, uiWidth);
    const int safeHeight = std::max(1, uiHeight);
    const float preferredScale = std::max(0.1f, m_layout.panelScale);
    const float fitPadding = std::max(0.0f, m_layout.fitPadding);
    const float availableWidth = std::max(1.0f, static_cast<float>(safeWidth) - fitPadding * 2.0f);
    const float availableHeight = std::max(1.0f, static_cast<float>(safeHeight) - fitPadding * 2.0f);
    const float fitScale = std::min(availableWidth / InventoryPanelLayout::kTextureWidth,
                                    availableHeight / InventoryPanelLayout::kTextureHeight);
    const float scale = std::max(0.1f, std::min(preferredScale, fitScale));

    ResolvedPanelRect rect;
    rect.scale = scale;
    rect.width = InventoryPanelLayout::kTextureWidth * scale;
    rect.height = InventoryPanelLayout::kTextureHeight * scale;
    rect.x = static_cast<float>(safeWidth) * m_layout.anchorX - rect.width * m_layout.pivotX + m_layout.offsetX * scale;
    rect.y =
        static_cast<float>(safeHeight) * m_layout.anchorY - rect.height * m_layout.pivotY + m_layout.offsetY * scale;
    return rect;
}
