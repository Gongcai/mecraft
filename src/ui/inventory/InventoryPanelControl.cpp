#include "InventoryPanelControl.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string_view>
#include <vector>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../crafting/CraftingSystem.h"
#include "../../item/Item.h"
#include "../../player/Inventory.h"
#include "../../renderer/renderers/HumanoidRenderer.h"
#include "../../renderer/core/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../../locale/LocaleManager.h"

namespace {
void addQuad(std::vector<float>& vertices,
             const float x0,
             const float y0,
             const float x1,
             const float y1,
             const float u0,
             const float v0,
             const float u1,
             const float v1)
{
    vertices.push_back(x0); vertices.push_back(y0); vertices.push_back(u0); vertices.push_back(v0);
    vertices.push_back(x1); vertices.push_back(y0); vertices.push_back(u1); vertices.push_back(v0);
    vertices.push_back(x1); vertices.push_back(y1); vertices.push_back(u1); vertices.push_back(v1);
    vertices.push_back(x0); vertices.push_back(y0); vertices.push_back(u0); vertices.push_back(v0);
    vertices.push_back(x1); vertices.push_back(y1); vertices.push_back(u1); vertices.push_back(v1);
    vertices.push_back(x0); vertices.push_back(y1); vertices.push_back(u0); vertices.push_back(v1);
}
}

void InventoryPanelControl::init(ResourceMgr& resourceMgr)
{
    m_resourceMgr = &resourceMgr;
    m_inventoryShader = resourceMgr.getShader("inventory");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_itemGrid.init(resourceMgr);
    m_craftingGrid.init(resourceMgr);
    m_tooltip.init(resourceMgr);
}

void InventoryPanelControl::shutdown()
{
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }

    m_tooltip.shutdown();
    m_craftingGrid.shutdown();
    m_itemGrid.shutdown();
    m_inventory = nullptr;
    m_craftingSystem = nullptr;
    m_inventoryShader = nullptr;
    m_resourceMgr = nullptr;
}

void InventoryPanelControl::renderSelf(const UIRenderContext& context) const
{
    auto* self = const_cast<InventoryPanelControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlotsFromInventory();

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
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
            if (craftSlot >= 0 && craftSlot < 4) {
                hoveredId = m_craftingGrid.getCraftingSlot(craftSlot);
            }
        }

        if (hoveredId != 0) {
            const ItemDef& def = ItemRegistry::get(hoveredId);
            const std::string name = context.localeManager
                ? context.localeManager->getItemName(def.namespacedId.path())
                : std::string(def.namespacedId.path());
            if (hoveredId != m_tooltipHoveredItemId) {
                m_tooltipHoveredItemId = hoveredId;
                m_tooltip.startHover(name, context.pointerX, context.pointerY,
                                     static_cast<float>(context.screenWidth),
                                     static_cast<float>(context.screenHeight),
                                     context.timeSeconds);
            } else if (m_tooltip.isHovering()) {
                m_tooltip.startHover(name, context.pointerX, context.pointerY,
                                     static_cast<float>(context.screenWidth),
                                     static_cast<float>(context.screenHeight),
                                     context.timeSeconds);
            }
        } else {
            m_tooltip.cancelHover();
            m_tooltipHoveredItemId = 0;
        }
    }
    m_tooltip.render(context);
}

UIEventResult InventoryPanelControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx)
{
    if (!visible) {
        return UIEventResult::Ignored;
    }
    syncSlotsFromInventory();

    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedScreenWidth, m_cachedScreenHeight);
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

void InventoryPanelControl::setVisible(bool isVisible)
{
    visible = isVisible;
    m_itemGrid.setVisible(isVisible);
    m_craftingGrid.setVisible(isVisible);
}

void InventoryPanelControl::setSlots(const Pickable::SlotInfo* slots, int count)
{
    m_useExternalSlots = true;
    m_itemGrid.setSlots(slots, count);
}

void InventoryPanelControl::setInventorySource(const Inventory* inventory)
{
    m_inventory = inventory;
    m_useExternalSlots = false;
}

void InventoryPanelControl::setLayout(const InventoryPanelLayout& layout)
{
    m_layout = layout;
    m_craftingGrid.setLayout(layout.craftingGrid);
    syncSlotsFromInventory();
}

const InventoryPanelLayout& InventoryPanelControl::getLayout() const
{
    return m_layout;
}

ItemGridControl& InventoryPanelControl::itemGrid()
{
    return m_itemGrid;
}

const ItemGridControl& InventoryPanelControl::itemGrid() const
{
    return m_itemGrid;
}

CraftingGridControl& InventoryPanelControl::craftingGrid()
{
    return m_craftingGrid;
}

const CraftingGridControl& InventoryPanelControl::craftingGrid() const
{
    return m_craftingGrid;
}

void InventoryPanelControl::setCraftingSystem(const CraftingSystem* craftingSystem)
{
    m_craftingSystem = craftingSystem;
}

void InventoryPanelControl::syncSlotsFromInventory()
{
    if (m_useExternalSlots) {
        return;
    }
    if (!m_inventory) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedScreenWidth, m_cachedScreenHeight);
    const float scale = panelRect.scale;

    std::array<Pickable::SlotInfo, Inventory::INVENTORY_SIZE> slots{};
    const int baseX = static_cast<int>(std::lround(panelRect.x + m_layout.gridOffsetX * scale));
    const int baseY = static_cast<int>(std::lround(panelRect.y + m_layout.gridOffsetY * scale));
    const int slotSize = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * scale)));
    const int colStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.columnGap) * scale)));
    const int rowStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.rowGap) * scale)));
    const int extraRow4 = static_cast<int>(std::lround(m_layout.row4ExtraGap * scale));

    int outIndex = 0;
    for (int row = 0; row < Inventory::INVENTORY_ROWS; ++row) {
        const int y = baseY + row * rowStep + (row >= 3 ? extraRow4 : 0);
        for (int col = 0; col < Inventory::INVENTORY_COLUMNS; ++col) {
            const int inventoryIndex = Inventory::toInventoryIndex(row, col);
            const ItemStack stack = m_inventory->getSlotStack(inventoryIndex);
            slots[static_cast<size_t>(outIndex)] = {
                baseX + col * colStep,
                y,
                slotSize,
                static_cast<int>(stack.itemId),
                static_cast<int>(stack.count)
            };
            ++outIndex;
        }
    }

    m_itemGrid.setSlots(slots.data(), static_cast<int>(slots.size()));
}

void InventoryPanelControl::syncCraftingGridPosition(const ResolvedPanelRect& panelRect)
{
    m_craftingGrid.setPanelOrigin(panelRect.x, panelRect.y, panelRect.scale);
    m_craftingGrid.setLayout(m_layout.craftingGrid);
}

void InventoryPanelControl::renderBackground(const UIRenderContext& context) const
{
    if (!m_inventoryShader || !m_resourceMgr || m_vao == 0 || m_vbo == 0) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const unsigned int texture = m_resourceMgr->getGuiTexture("inventory");
    if (texture == 0) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float x0 = panelRect.x;
    const float y0 = panelRect.y;
    const float x1 = panelRect.x + panelRect.width;
    const float y1 = panelRect.y + panelRect.height;

    const float vertices[] = {
        x0, y0, 0.0f, 0.0f,
        x1, y0, 1.0f, 0.0f,
        x1, y1, 1.0f, 1.0f,
        x0, y0, 0.0f, 0.0f,
        x1, y1, 1.0f, 1.0f,
        x0, y1, 0.0f, 1.0f,
    };

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(context.screenWidth), static_cast<float>(context.screenHeight)));
    m_inventoryShader->setVec4("uTintColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    m_inventoryShader->setInt("uAtlas", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void InventoryPanelControl::renderPlayerPreview(const UIRenderContext& context,
                                                const ResolvedPanelRect& panelRect) const
{
    if (!context.humanoidRenderer || context.uiScale <= 0.0f) {
        return;
    }

    const float previewWidth = std::max(1.0f, (m_layout.playerPreviewX1 - m_layout.playerPreviewX0) * panelRect.scale);
    const float previewHeight = std::max(1.0f, (m_layout.playerPreviewY1 - m_layout.playerPreviewY0) * panelRect.scale);
    const float previewX = panelRect.x + m_layout.playerPreviewX0 * panelRect.scale;
    const float previewY = panelRect.y
        + (InventoryPanelLayout::kTextureHeight - m_layout.playerPreviewY1) * panelRect.scale;
    const float pointerBottomY = static_cast<float>(context.screenHeight) - context.pointerY;

    context.humanoidRenderer->renderInventoryPreview(previewX,
                                                     previewY,
                                                     previewWidth,
                                                     previewHeight,
                                                     context.uiScale,
                                                     context.pointerX,
                                                     pointerBottomY,
                                                     context.timeSeconds);
}

void InventoryPanelControl::renderDraggedItem(const UIRenderContext& context) const
{
    if (!context.hasDraggedItem || context.draggedItemId <= 0 || !m_inventoryShader || !m_resourceMgr) {
        return;
    }

    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resourceMgr->getItemTextureAtlas();

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float iconSize = std::max(1.0f, m_layout.slotSize * panelRect.scale);
    constexpr float kDragCursorOffsetPx = 1.0f;
    const float x0 = context.pointerX + kDragCursorOffsetPx;
    const float topY0 = context.pointerY + kDragCursorOffsetPx;
    const float x1 = x0 + iconSize;
    const float topY1 = topY0 + iconSize;
    const float y0 = static_cast<float>(context.screenHeight) - topY1;
    const float y1 = static_cast<float>(context.screenHeight) - topY0;

    const auto draggedItem = static_cast<ItemID>(context.draggedItemId);
    const ItemDef& itemDef = ItemRegistry::get(draggedItem);
    const bool hasItemTextures = (itemTextureAtlas.textureID != 0 && itemTextureAtlas.tilesPerRow > 0);
    const bool hasFallbackIcons = (itemIconAtlas.textureID != 0 && itemIconAtlas.tilesPerRow > 0);
    const int itemTileIndex = hasItemTextures ? m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName) : -1;
    const bool useItemTexture = itemTileIndex >= 0;
    if (!useItemTexture && !hasFallbackIcons) {
        return;
    }

    const auto uv = useItemTexture
        ? itemTextureAtlas.getUV(itemTileIndex)
        : itemIconAtlas.getUV(static_cast<int>(itemDef.renderBlock));
    std::vector<float> vertices;
    vertices.reserve(24);
    addQuad(vertices, x0, y0, x1, y1, uv.first.x, uv.first.y, uv.second.x, uv.second.y);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(context.screenWidth), static_cast<float>(context.screenHeight)));
    m_inventoryShader->setVec4("uTintColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.95f));
    m_inventoryShader->setInt("uAtlas", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, useItemTexture ? itemTextureAtlas.textureID : itemIconAtlas.textureID);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

InventoryPanelControl::ResolvedPanelRect InventoryPanelControl::resolvePanelRect(const int screenWidth,
                                                                                 const int screenHeight) const
{
    const int safeWidth = std::max(1, screenWidth);
    const int safeHeight = std::max(1, screenHeight);
    const float scale = std::max(0.1f, m_layout.panelScale);

    ResolvedPanelRect rect;
    rect.scale = scale;
    rect.width = InventoryPanelLayout::kTextureWidth * scale;
    rect.height = InventoryPanelLayout::kTextureHeight * scale;
    rect.x = static_cast<float>(safeWidth) * m_layout.anchorX + m_layout.offsetX;
    rect.y = static_cast<float>(safeHeight) * m_layout.anchorY + m_layout.offsetY;
    return rect;
}
