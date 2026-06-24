#include "FurnacePanelControl.h"

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
constexpr float kGuiAtlasWidth = 256.0f;
constexpr float kGuiAtlasHeight = 256.0f;

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

float textureU(const float x) {
    return x / kGuiAtlasWidth;
}

float textureVTop(const float y) {
    return 1.0f - y / kGuiAtlasHeight;
}
}

void FurnacePanelControl::init(ResourceMgr& resourceMgr) {
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

    m_furnaceGrid.init(resourceMgr);
    m_playerGrid.init(resourceMgr);
    m_tooltip.init(resourceMgr);
}

void FurnacePanelControl::shutdown() {
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
    m_furnaceGrid.shutdown();
    m_furnace = nullptr;
    m_playerInventory = nullptr;
    m_inventoryShader = nullptr;
    m_resourceMgr = nullptr;
}

UIEventResult FurnacePanelControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) {
        return UIEventResult::Ignored;
    }

    syncSlots();

    UIEventResult result = m_furnaceGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        m_playerGrid.clearLastActivatedIndex();
        return result;
    }
    if (result == UIEventResult::Handled) {
        return result;
    }

    result = m_playerGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        m_furnaceGrid.clearLastActivatedIndex();
    }
    return result;
}

void FurnacePanelControl::setVisible(const bool isVisible) {
    visible = isVisible;
    m_furnaceGrid.setVisible(isVisible);
    m_playerGrid.setVisible(isVisible);
    if (!isVisible) {
        clearActivations();
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    }
}

void FurnacePanelControl::setFurnaceSource(const FurnaceInventory* furnace) {
    m_furnace = furnace;
    syncSlots();
}

void FurnacePanelControl::setPlayerInventorySource(const Inventory* inventory) {
    m_playerInventory = inventory;
    syncSlots();
}

void FurnacePanelControl::setProgress(const float burnFraction, const float cookFraction) {
    m_burnFraction = std::clamp(burnFraction, 0.0f, 1.0f);
    m_cookFraction = std::clamp(cookFraction, 0.0f, 1.0f);
}

int FurnacePanelControl::getFurnaceLastActivatedSlot() const {
    return m_furnaceGrid.getLastActivatedIndex();
}

int FurnacePanelControl::getPlayerLastActivatedSlot() const {
    return m_playerGrid.getLastActivatedIndex();
}

int FurnacePanelControl::getFurnaceHoveredSlot() const {
    return m_furnaceGrid.getHoveredIndex();
}

int FurnacePanelControl::getPlayerHoveredSlot() const {
    return m_playerGrid.getHoveredIndex();
}

void FurnacePanelControl::clearActivations() {
    m_furnaceGrid.clearLastActivatedIndex();
    m_playerGrid.clearLastActivatedIndex();
}

void FurnacePanelControl::renderSelf(const UIRenderContext& context) const {
    auto* self = const_cast<FurnacePanelControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlots();

    renderBackground(context);
    renderProgress(context);
    m_furnaceGrid.render(context);
    m_playerGrid.render(context);
    renderDraggedItem(context);
    renderTooltip(context);
}

FurnacePanelControl::ResolvedPanelRect FurnacePanelControl::resolvePanelRect(const int screenWidth,
                                                                             const int screenHeight) const {
    const int safeWidth = std::max(1, screenWidth);
    const int safeHeight = std::max(1, screenHeight);
    const float scale = std::max(0.1f, m_layout.panelScale);

    ResolvedPanelRect rect;
    rect.scale = scale;
    rect.width = FurnacePanelLayout::kTextureWidth * scale;
    rect.height = FurnacePanelLayout::kTextureHeight * scale;
    rect.x = static_cast<float>(safeWidth) * m_layout.anchorX + m_layout.offsetX * scale;
    rect.y = static_cast<float>(safeHeight) * m_layout.anchorY + m_layout.offsetY * scale;
    return rect;
}

void FurnacePanelControl::syncSlots() {
    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedScreenWidth, m_cachedScreenHeight);
    const float scale = panelRect.scale;
    const int slotSize = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * scale)));
    const int step = slotSize;

    std::array<Pickable::SlotInfo, FurnaceInventory::SLOT_COUNT> furnaceSlots{};
    const std::array<std::pair<float, float>, FurnaceInventory::SLOT_COUNT> furnaceSlotPositions = {{
        {m_layout.inputSlotX, m_layout.inputSlotY},
        {m_layout.fuelSlotX, m_layout.fuelSlotY},
        {m_layout.outputSlotX, m_layout.outputSlotY},
    }};

    for (int slot = 0; slot < FurnaceInventory::SLOT_COUNT; ++slot) {
        const auto [slotX, slotY] = furnaceSlotPositions[static_cast<std::size_t>(slot)];
        const ItemStack stack = m_furnace ? m_furnace->getSlotStack(slot) : ItemStack{};
        furnaceSlots[static_cast<std::size_t>(slot)] = {
            static_cast<int>(std::lround(panelRect.x + slotX * scale)),
            static_cast<int>(std::lround(panelRect.y + slotY * scale)),
            slotSize,
            static_cast<int>(stack.itemId),
            static_cast<int>(stack.count)
        };
    }
    m_furnaceGrid.setSlots(furnaceSlots.data(), static_cast<int>(furnaceSlots.size()));

    std::array<Pickable::SlotInfo, Inventory::INVENTORY_SIZE> playerSlots{};
    const int playerBaseX = static_cast<int>(std::lround(panelRect.x + m_layout.playerGridOffsetX * scale));
    const int playerBaseY = static_cast<int>(std::lround(panelRect.y + m_layout.playerGridOffsetY * scale));
    const int hotbarY = static_cast<int>(std::lround(panelRect.y + m_layout.hotbarOffsetY * scale));
    int outIndex = 0;
    for (int row = 0; row < Inventory::INVENTORY_ROWS; ++row) {
        const int slotY = row < Inventory::MAIN_INVENTORY_ROWS ? playerBaseY + row * step : hotbarY;
        for (int col = 0; col < Inventory::INVENTORY_COLUMNS; ++col) {
            const int inventoryIndex = Inventory::toInventoryIndex(row, col);
            const ItemStack stack = m_playerInventory ? m_playerInventory->getSlotStack(inventoryIndex) : ItemStack{};
            playerSlots[static_cast<std::size_t>(outIndex)] = {
                playerBaseX + col * step,
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

void FurnacePanelControl::renderBackground(const UIRenderContext& context) const {
    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    drawTextureQuad(context,
                    panelRect.x,
                    panelRect.y,
                    panelRect.x + panelRect.width,
                    panelRect.y + panelRect.height,
                    textureU(0.0f),
                    textureVTop(FurnacePanelLayout::kTextureHeight),
                    textureU(FurnacePanelLayout::kTextureWidth),
                    textureVTop(0.0f));
}

void FurnacePanelControl::renderProgress(const UIRenderContext& context) const {
    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float scale = panelRect.scale;

    const float flameHeight = std::round(14.0f * m_burnFraction);
    if (flameHeight > 0.0f) {
        const float srcX0 = 176.0f;
        const float srcY0 = 14.0f - flameHeight;
        const float srcX1 = 190.0f;
        const float srcY1 = 14.0f;
        const float dstX0 = panelRect.x + 57.0f * scale;
        const float dstY0 = panelRect.y + (36.0f + 14.0f - flameHeight) * scale;
        drawTextureQuad(context,
                        dstX0,
                        dstY0,
                        dstX0 + 14.0f * scale,
                        dstY0 + flameHeight * scale,
                        textureU(srcX0),
                        textureVTop(srcY1),
                        textureU(srcX1),
                        textureVTop(srcY0));
    }

    const float arrowWidth = std::round(24.0f * m_cookFraction);
    if (arrowWidth > 0.0f) {
        const float dstX0 = panelRect.x + 79.0f * scale;
        const float dstY0 = panelRect.y + 34.0f * scale;
        drawTextureQuad(context,
                        dstX0,
                        dstY0,
                        dstX0 + arrowWidth * scale,
                        dstY0 + 17.0f * scale,
                        textureU(176.0f),
                        textureVTop(31.0f),
                        textureU(176.0f + arrowWidth),
                        textureVTop(14.0f));
    }
}

void FurnacePanelControl::renderDraggedItem(const UIRenderContext& context) const {
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
    const bool hasBlockIconAtlas = itemIconAtlas.textureID != 0 && itemIconAtlas.tilesPerRow > 0;
    const bool useBakedBlockIcon = hasBlockIconAtlas && ui::shouldUseBakedBlockIcon(itemDef);
    const int itemTileIndex = (!useBakedBlockIcon && hasItemTextures)
        ? m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName)
        : -1;
    const bool useItemTexture = !useBakedBlockIcon && itemTileIndex >= 0;
    if (!useBakedBlockIcon && !useItemTexture && !hasBlockIconAtlas) {
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
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(context.screenWidth),
                                                         static_cast<float>(context.screenHeight)));
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

void FurnacePanelControl::renderTooltip(const UIRenderContext& context) const {
    if (context.hasDraggedItem) {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
        return;
    }

    ItemID hoveredId = m_furnaceGrid.getHoveredItemId();
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

void FurnacePanelControl::drawTextureQuad(const UIRenderContext& context,
                                          const float x0,
                                          const float y0,
                                          const float x1,
                                          const float y1,
                                          const float u0,
                                          const float v0,
                                          const float u1,
                                          const float v1) const {
    if (!m_inventoryShader || !m_resourceMgr || m_vao == 0 || m_vbo == 0) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const unsigned int texture = m_resourceMgr->getGuiTexture("furnace");
    if (texture == 0) {
        return;
    }

    std::vector<float> vertices;
    vertices.reserve(24);
    addQuad(vertices, x0, y0, x1, y1, u0, v0, u1, v1);

    const UIRenderUtils::GLStateGuard glState;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(context.screenWidth),
                                                         static_cast<float>(context.screenHeight)));
    m_inventoryShader->setVec4("uTintColor", glm::vec4(1.0f));
    m_inventoryShader->setInt("uAtlas", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
