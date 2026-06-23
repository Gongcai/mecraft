#include "ChestPanelControl.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../item/Item.h"
#include "../../locale/LocaleManager.h"
#include "../../player/Inventory.h"
#include "../../renderer/core/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderUtils.h"

namespace {
constexpr float kChestGuiAtlasWidth = 256.0f;
constexpr float kChestGuiAtlasHeight = 256.0f;
constexpr float kChestGuiVisibleWidth = ChestPanelLayout::kTextureWidth;
constexpr float kChestGuiVisibleHeight = ChestPanelLayout::kTextureHeight;
constexpr float kChestGuiVisibleU0 = 0.0f;
constexpr float kChestGuiVisibleU1 = kChestGuiVisibleWidth / kChestGuiAtlasWidth;
// GUI textures are flipped on load, so the source top-left crop maps to the upper GL V range.
constexpr float kChestGuiVisibleV0 = 1.0f - kChestGuiVisibleHeight / kChestGuiAtlasHeight;
constexpr float kChestGuiVisibleV1 = 1.0f;

void addQuad(std::vector<float>& vertices,
             const float x0,
             const float y0,
             const float x1,
             const float y1,
             const float u0,
             const float v0,
             const float u1,
             const float v1) {
    vertices.push_back(x0); vertices.push_back(y0); vertices.push_back(u0); vertices.push_back(v0);
    vertices.push_back(x1); vertices.push_back(y0); vertices.push_back(u1); vertices.push_back(v0);
    vertices.push_back(x1); vertices.push_back(y1); vertices.push_back(u1); vertices.push_back(v1);
    vertices.push_back(x0); vertices.push_back(y0); vertices.push_back(u0); vertices.push_back(v0);
    vertices.push_back(x1); vertices.push_back(y1); vertices.push_back(u1); vertices.push_back(v1);
    vertices.push_back(x0); vertices.push_back(y1); vertices.push_back(u0); vertices.push_back(v1);
}
}

void ChestPanelControl::init(ResourceMgr& resourceMgr) {
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

    m_chestGrid.init(resourceMgr);
    m_playerGrid.init(resourceMgr);
    m_tooltip.init(resourceMgr);
}

void ChestPanelControl::shutdown() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }

    m_tooltip.shutdown();
    m_playerGrid.shutdown();
    m_chestGrid.shutdown();
    m_chest = nullptr;
    m_playerInventory = nullptr;
    m_inventoryShader = nullptr;
    m_resourceMgr = nullptr;
}

UIEventResult ChestPanelControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) {
        return UIEventResult::Ignored;
    }

    static_cast<void>(ctx);
    syncSlots();

    UIEventResult result = m_chestGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        m_playerGrid.clearLastActivatedIndex();
        return result;
    }
    if (result == UIEventResult::Handled) {
        return result;
    }

    result = m_playerGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        m_chestGrid.clearLastActivatedIndex();
    }
    return result;
}

void ChestPanelControl::setVisible(const bool isVisible) {
    visible = isVisible;
    m_chestGrid.setVisible(isVisible);
    m_playerGrid.setVisible(isVisible);
    if (!isVisible) {
        clearActivations();
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    }
}

void ChestPanelControl::setChestSource(const ChestInventory* chest) {
    m_chest = chest;
    syncSlots();
}

void ChestPanelControl::setPlayerInventorySource(const Inventory* inventory) {
    m_playerInventory = inventory;
    syncSlots();
}

void ChestPanelControl::setLayout(const ChestPanelLayout& layout) {
    m_layout = layout;
    syncSlots();
}

const ChestPanelLayout& ChestPanelControl::getLayout() const {
    return m_layout;
}

int ChestPanelControl::getChestLastActivatedSlot() const {
    return m_chestGrid.getLastActivatedIndex();
}

int ChestPanelControl::getPlayerLastActivatedSlot() const {
    return m_playerGrid.getLastActivatedIndex();
}

int ChestPanelControl::getChestHoveredSlot() const {
    return m_chestGrid.getHoveredIndex();
}

int ChestPanelControl::getPlayerHoveredSlot() const {
    return m_playerGrid.getHoveredIndex();
}

void ChestPanelControl::clearActivations() {
    m_chestGrid.clearLastActivatedIndex();
    m_playerGrid.clearLastActivatedIndex();
}

void ChestPanelControl::renderSelf(const UIRenderContext& context) const {
    auto* self = const_cast<ChestPanelControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlots();

    renderBackground(context);
    m_chestGrid.render(context);
    m_playerGrid.render(context);
    renderDraggedItem(context);
    renderTooltip(context);
}

ChestPanelControl::ResolvedPanelRect ChestPanelControl::resolvePanelRect(const int screenWidth,
                                                                         const int screenHeight) const {
    const int safeWidth = std::max(1, screenWidth);
    const int safeHeight = std::max(1, screenHeight);
    const float scale = std::max(0.1f, m_layout.panelScale);

    ResolvedPanelRect rect;
    rect.scale = scale;
    rect.width = ChestPanelLayout::kTextureWidth * scale;
    rect.height = ChestPanelLayout::kTextureHeight * scale;
    rect.x = static_cast<float>(safeWidth) * m_layout.anchorX + m_layout.offsetX;
    rect.y = static_cast<float>(safeHeight) * m_layout.anchorY + m_layout.offsetY;
    return rect;
}

void ChestPanelControl::syncSlots() {
    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedScreenWidth, m_cachedScreenHeight);
    const float scale = panelRect.scale;
    const int slotSize = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * scale)));
    const int colStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.columnGap) * scale)));
    const int rowStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.rowGap) * scale)));

    std::array<Pickable::SlotInfo, ChestInventory::SLOT_COUNT> chestSlots{};
    const int chestBaseX = static_cast<int>(std::lround(panelRect.x + m_layout.chestGridOffsetX * scale));
    const int chestBaseY = static_cast<int>(std::lround(panelRect.y + m_layout.chestGridOffsetY * scale));
    for (int slot = 0; slot < ChestInventory::SLOT_COUNT; ++slot) {
        const int row = slot / ChestInventory::COLUMNS;
        const int col = slot % ChestInventory::COLUMNS;
        const ItemStack stack = m_chest ? m_chest->getSlotStack(slot) : ItemStack{};
        chestSlots[static_cast<std::size_t>(slot)] = {
            chestBaseX + col * colStep,
            chestBaseY + row * rowStep,
            slotSize,
            static_cast<int>(stack.itemId),
            static_cast<int>(stack.count)
        };
    }
    m_chestGrid.setSlots(chestSlots.data(), static_cast<int>(chestSlots.size()));

    std::array<Pickable::SlotInfo, Inventory::INVENTORY_SIZE> playerSlots{};
    const int playerBaseX = static_cast<int>(std::lround(panelRect.x + m_layout.playerGridOffsetX * scale));
    const int playerBaseY = static_cast<int>(std::lround(panelRect.y + m_layout.playerGridOffsetY * scale));
    const int hotbarY = static_cast<int>(std::lround(panelRect.y + m_layout.hotbarOffsetY * scale));
    int outIndex = 0;
    for (int row = 0; row < Inventory::INVENTORY_ROWS; ++row) {
        const int slotY = (row < Inventory::MAIN_INVENTORY_ROWS)
            ? playerBaseY + row * rowStep
            : hotbarY;
        for (int col = 0; col < Inventory::INVENTORY_COLUMNS; ++col) {
            const int inventoryIndex = Inventory::toInventoryIndex(row, col);
            const ItemStack stack = m_playerInventory ? m_playerInventory->getSlotStack(inventoryIndex) : ItemStack{};
            playerSlots[static_cast<std::size_t>(outIndex)] = {
                playerBaseX + col * colStep,
                slotY,
                slotSize,
                static_cast<int>(stack.itemId),
                static_cast<int>(stack.count)
            };
            ++outIndex;
        }
    }
    m_playerGrid.setSlots(playerSlots.data(), static_cast<int>(playerSlots.size()));
}

void ChestPanelControl::renderBackground(const UIRenderContext& context) const {
    if (!m_inventoryShader || !m_resourceMgr || m_vao == 0 || m_vbo == 0) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const unsigned int texture = m_resourceMgr->getGuiTexture("chest_generic_54");
    if (texture == 0) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float x0 = panelRect.x;
    const float y0 = panelRect.y;
    const float x1 = panelRect.x + panelRect.width;
    const float y1 = panelRect.y + panelRect.height;

    const float vertices[] = {
        x0, y0, kChestGuiVisibleU0, kChestGuiVisibleV0,
        x1, y0, kChestGuiVisibleU1, kChestGuiVisibleV0,
        x1, y1, kChestGuiVisibleU1, kChestGuiVisibleV1,
        x0, y0, kChestGuiVisibleU0, kChestGuiVisibleV0,
        x1, y1, kChestGuiVisibleU1, kChestGuiVisibleV1,
        x0, y1, kChestGuiVisibleU0, kChestGuiVisibleV1,
    };

    const UIRenderUtils::GLStateGuard glState;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(context.screenWidth), static_cast<float>(context.screenHeight)));
    m_inventoryShader->setVec4("uTintColor", glm::vec4(1.0f));
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
}

void ChestPanelControl::renderDraggedItem(const UIRenderContext& context) const {
    if (!context.hasDraggedItem || context.draggedItemId <= 0 || !m_inventoryShader || !m_resourceMgr) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resourceMgr->getItemTextureAtlas();
    const auto draggedItem = static_cast<ItemID>(context.draggedItemId);
    const ItemDef& itemDef = ItemRegistry::get(draggedItem);

    const bool hasItemTextures = itemTextureAtlas.textureID != 0 && itemTextureAtlas.tilesPerRow > 0;
    const bool hasFallbackIcons = itemIconAtlas.textureID != 0 && itemIconAtlas.tilesPerRow > 0;
    const bool useBakedBlockIcon = hasFallbackIcons && ui::shouldUseBakedBlockIcon(itemDef);
    const int itemTileIndex = (!useBakedBlockIcon && hasItemTextures) ? m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName) : -1;
    const bool useItemTexture = !useBakedBlockIcon && itemTileIndex >= 0;
    if (!useBakedBlockIcon && !useItemTexture && !hasFallbackIcons) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float iconSize = std::max(1.0f, m_layout.slotSize * panelRect.scale);
    constexpr float kDragCursorOffsetPx = 1.0f;
    const float x0 = context.pointerX + kDragCursorOffsetPx;
    const float topY0 = context.pointerY + kDragCursorOffsetPx;
    const float x1 = x0 + iconSize;
    const float topY1 = topY0 + iconSize;
    const float y0 = static_cast<float>(context.screenHeight) - topY1;
    const float y1 = static_cast<float>(context.screenHeight) - topY0;

    const auto uv = useItemTexture
        ? itemTextureAtlas.getUV(itemTileIndex)
        : itemIconAtlas.getUV(static_cast<int>(itemDef.renderBlock));
    std::vector<float> vertices;
    vertices.reserve(24);
    addQuad(vertices, x0, y0, x1, y1, uv.first.x, uv.first.y, uv.second.x, uv.second.y);

    const UIRenderUtils::GLStateGuard glState;
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
}

void ChestPanelControl::renderTooltip(const UIRenderContext& context) const {
    if (context.hasDraggedItem) {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
        return;
    }

    ItemID hoveredId = m_chestGrid.getHoveredItemId();
    if (hoveredId == 0) {
        hoveredId = m_playerGrid.getHoveredItemId();
    }

    if (hoveredId != 0) {
        const ItemDef& def = ItemRegistry::get(hoveredId);
        const std::string name = context.localeManager
            ? context.localeManager->getItemName(def.namespacedId.path())
            : std::string(def.namespacedId.path());
        if (hoveredId != m_tooltipHoveredItemId) {
            m_tooltipHoveredItemId = hoveredId;
        }
        m_tooltip.startHover(name,
                             context.pointerX,
                             context.pointerY,
                             static_cast<float>(context.screenWidth),
                             static_cast<float>(context.screenHeight),
                             context.timeSeconds);
    } else {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    }

    m_tooltip.render(context);
}
