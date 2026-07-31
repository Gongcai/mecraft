#include "DataDrivenContainerPanelControl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/vec4.hpp>

#include "../../item/Item.h"
#include "../../locale/LocaleManager.h"
#include "../../player/Inventory.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/ResourceMgr.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderer.h"

namespace {
struct ImageTexturePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

static_assert(sizeof(ImageTexturePushConstants) == 64u);

[[nodiscard]] RhiRect2D containerScissor(const UIRenderContext& context) {
    if (context.hasScissor) {
        return context.scissor;
    }
    return {0, 0,
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenWidth) * context.pixelScale()))),
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenHeight) * context.pixelScale())))};
}
} // namespace

void DataDrivenContainerPanelControl::init(ResourceMgr& resourceMgr) {
    UIWidget::init(resourceMgr);
    m_resourceMgr = &resourceMgr;

    m_containerGrid.init(resourceMgr);
    m_playerGrid.init(resourceMgr);
    m_tooltip.init(resourceMgr);
}

void DataDrivenContainerPanelControl::shutdown() {
    m_tooltip.shutdown();
    m_playerGrid.shutdown();
    m_containerGrid.shutdown();
    m_containerSlotMapping.clear();
    m_playerSlotMapping.clear();
    m_storageInventory = nullptr;
    m_machine = nullptr;
    m_playerInventory = nullptr;
    m_definition = nullptr;
    m_resourceMgr = nullptr;
    UIWidget::shutdown();
}

UIEventResult DataDrivenContainerPanelControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) {
        return UIEventResult::Ignored;
    }

    syncSlots();

    UIEventResult result = m_containerGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        m_playerGrid.clearLastActivatedIndex();
        return result;
    }
    if (result == UIEventResult::Handled) {
        return result;
    }

    result = m_playerGrid.onInput(event, ctx);
    if (result == UIEventResult::Consumed) {
        m_containerGrid.clearLastActivatedIndex();
    }
    return result;
}

void DataDrivenContainerPanelControl::setVisible(const bool isVisible) {
    if (isVisible && m_definition == nullptr) {
        std::cerr << "Data-driven container panel requires a UI definition.\n";
        visible = false;
        m_containerGrid.setVisible(false);
        m_playerGrid.setVisible(false);
        return;
    }
    visible = isVisible;
    m_containerGrid.setVisible(isVisible);
    m_playerGrid.setVisible(isVisible);
    if (!isVisible) {
        clearActivations();
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    }
}

void DataDrivenContainerPanelControl::setDefinition(const ui::ContainerUiDef& definition) {
    m_definition = &definition;
    syncSlots();
}

void DataDrivenContainerPanelControl::setStorageSource(const BlockEntityInventory* storageInventory) {
    m_storageInventory = storageInventory;
    if (storageInventory != nullptr) {
        m_machine = nullptr;
    }
    syncSlots();
}

void DataDrivenContainerPanelControl::setMachineSource(const MachineInventory* machine) {
    m_machine = machine;
    if (machine != nullptr) {
        m_storageInventory = nullptr;
    }
    syncSlots();
}

void DataDrivenContainerPanelControl::setPlayerInventorySource(const Inventory* inventory) {
    m_playerInventory = inventory;
    syncSlots();
}

void DataDrivenContainerPanelControl::setProgress(const float burnFraction, const float cookFraction) {
    m_burnFraction = std::clamp(burnFraction, 0.0f, 1.0f);
    m_cookFraction = std::clamp(cookFraction, 0.0f, 1.0f);
}

int DataDrivenContainerPanelControl::getContainerLastActivatedSlot() const {
    return mapContainerGridIndex(m_containerGrid.getLastActivatedIndex());
}

int DataDrivenContainerPanelControl::getPlayerLastActivatedSlot() const {
    return mapPlayerGridIndex(m_playerGrid.getLastActivatedIndex());
}

int DataDrivenContainerPanelControl::getContainerHoveredSlot() const {
    return mapContainerGridIndex(m_containerGrid.getHoveredIndex());
}

int DataDrivenContainerPanelControl::getPlayerHoveredSlot() const {
    return mapPlayerGridIndex(m_playerGrid.getHoveredIndex());
}

void DataDrivenContainerPanelControl::clearActivations() {
    m_containerGrid.clearLastActivatedIndex();
    m_playerGrid.clearLastActivatedIndex();
}

void DataDrivenContainerPanelControl::renderSelf(const UIRenderContext& context) const {
    auto* self = const_cast<DataDrivenContainerPanelControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlots();

    renderBackground(context);
    renderProgressBars(context);
    m_containerGrid.render(context);
    m_playerGrid.render(context);
    renderDraggedItem(context);
    renderTooltip(context);
}

const ui::ContainerUiDef& DataDrivenContainerPanelControl::requireDefinition() const {
    if (m_definition == nullptr) {
        std::cerr << "Data-driven container panel requires a UI definition.\n";
        std::abort();
    }
    return *m_definition;
}

DataDrivenContainerPanelControl::ResolvedPanelRect
DataDrivenContainerPanelControl::resolvePanelRect(const int screenWidth, const int screenHeight) const {
    const ui::ContainerUiDef& def = requireDefinition();
    const int safeWidth = std::max(1, screenWidth);
    const int safeHeight = std::max(1, screenHeight);
    const float preferredScale = std::max(0.1f, def.scale);
    const float fitPadding = std::max(0.0f, def.fitPadding);
    const float availableWidth = std::max(1.0f, static_cast<float>(safeWidth) - fitPadding * 2.0f);
    const float availableHeight = std::max(1.0f, static_cast<float>(safeHeight) - fitPadding * 2.0f);
    const float fitScale = std::min(availableWidth / def.width, availableHeight / def.height);
    const float scale = std::max(0.1f, std::min(preferredScale, fitScale));

    ResolvedPanelRect rect;
    rect.scale = scale;
    rect.width = def.width * scale;
    rect.height = def.height * scale;
    rect.x = static_cast<float>(safeWidth) * def.anchorX - rect.width * def.pivotX + def.offsetX * scale;
    rect.y = static_cast<float>(safeHeight) * def.anchorY - rect.height * def.pivotY + def.offsetY * scale;
    return rect;
}

int DataDrivenContainerPanelControl::mapContainerGridIndex(const int gridIndex) const {
    if (gridIndex < 0 || gridIndex >= static_cast<int>(m_containerSlotMapping.size())) {
        return -1;
    }
    return m_containerSlotMapping[static_cast<std::size_t>(gridIndex)];
}

int DataDrivenContainerPanelControl::mapPlayerGridIndex(const int gridIndex) const {
    if (gridIndex < 0 || gridIndex >= static_cast<int>(m_playerSlotMapping.size())) {
        return -1;
    }
    return m_playerSlotMapping[static_cast<std::size_t>(gridIndex)];
}

void DataDrivenContainerPanelControl::syncSlots() {
    if (m_definition == nullptr) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedScreenWidth, m_cachedScreenHeight);
    std::vector<Pickable::SlotInfo> containerSlots;
    std::vector<Pickable::SlotInfo> playerSlots;
    std::vector<int> containerSlotMapping;
    std::vector<int> playerSlotMapping;

    for (const ui::ContainerSlotGroupDef& group : m_definition->slotGroups) {
        if (group.kind == ui::ContainerSlotGroupKind::Container) {
            appendSlotsForGroup(group, panelRect, true, containerSlots, &containerSlotMapping);
        } else if (group.kind == ui::ContainerSlotGroupKind::PlayerInventory) {
            appendSlotsForGroup(group, panelRect, false, playerSlots, &playerSlotMapping);
        }
    }

    m_containerSlotMapping = std::move(containerSlotMapping);
    m_playerSlotMapping = std::move(playerSlotMapping);
    if (containerSlots.empty()) {
        m_containerGrid.clearSlots();
    } else {
        m_containerGrid.setSlots(containerSlots.data(), static_cast<int>(containerSlots.size()));
    }
    if (playerSlots.empty()) {
        m_playerGrid.clearSlots();
    } else {
        m_playerGrid.setSlots(playerSlots.data(), static_cast<int>(playerSlots.size()));
    }
}

void DataDrivenContainerPanelControl::appendSlotsForGroup(const ui::ContainerSlotGroupDef& group,
                                                          const ResolvedPanelRect& panelRect, const bool containerGroup,
                                                          std::vector<Pickable::SlotInfo>& outSlots,
                                                          std::vector<int>* outSlotMapping) const {
    const float scale = panelRect.scale;
    const int slotSize = std::max(1, static_cast<int>(std::lround(group.slotSize * scale)));
    const int colStep = std::max(1, static_cast<int>(std::lround((group.slotSize + group.columnGap) * scale)));
    const int rowStep = std::max(1, static_cast<int>(std::lround((group.slotSize + group.rowGap) * scale)));
    const int baseX = static_cast<int>(std::lround(panelRect.x + group.x * scale));
    const int baseY = static_cast<int>(std::lround(panelRect.y + group.y * scale));

    for (int row = 0; row < group.rows; ++row) {
        const int rowExtraGap = row >= 3 ? static_cast<int>(std::lround(group.row4ExtraGap * scale)) : 0;
        for (int col = 0; col < group.columns; ++col) {
            const int slot = group.firstSlot + row * group.columns + col;
            ItemStack stack;
            if (containerGroup) {
                if (m_storageInventory != nullptr && m_storageInventory->isValidSlot(slot)) {
                    stack = m_storageInventory->getSlotStack(slot);
                } else if (m_machine != nullptr && m_machine->isValidSlot(slot)) {
                    stack = m_machine->getSlotStack(slot);
                }
            } else if (m_playerInventory != nullptr && m_playerInventory->isValidSlot(slot)) {
                stack = m_playerInventory->getSlotStack(slot);
            }
            outSlots.push_back({baseX + col * colStep, baseY + row * rowStep + rowExtraGap, slotSize,
                                static_cast<int>(stack.itemId), static_cast<int>(stack.count)});
            if (outSlotMapping != nullptr) {
                outSlotMapping->push_back(slot);
            }
        }
    }
}

void DataDrivenContainerPanelControl::renderBackground(const UIRenderContext& context) const {
    if (m_resourceMgr == nullptr) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const ui::ContainerUiDef& def = requireDefinition();
    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float x0 = panelRect.x;
    const float y0 = panelRect.y;
    const float x1 = panelRect.x + panelRect.width;
    const float y1 = panelRect.y + panelRect.height;
    const float u0 = 0.0f;
    const float u1 = def.width / def.textureWidth;
    const float v0 = 1.0f - def.height / def.textureHeight;
    const float v1 = 1.0f;

    drawTextureQuad(context, m_resourceMgr->getGuiTextureHandle(def.backgroundTexture), x0, y0, x1, y1, u0, v0, u1, v1,
                    1.0f);
}

void DataDrivenContainerPanelControl::renderProgressBars(const UIRenderContext& context) const {
    if (m_definition == nullptr || m_resourceMgr == nullptr || m_definition->progressBars.empty()) {
        return;
    }
    const ui::ContainerUiDef& def = requireDefinition();
    const RhiTextureHandle backgroundTexture = m_resourceMgr->getGuiTextureHandle(def.backgroundTexture);
    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float scale = panelRect.scale;

    for (const ui::ContainerProgressDef& progress : def.progressBars) {
        const float fraction = progress.kind == ui::ContainerProgressKind::Burn ? m_burnFraction : m_cookFraction;
        if (fraction <= 0.0f) {
            continue;
        }

        if (progress.direction == "up") {
            const float visibleHeight = std::round(progress.height * fraction);
            if (visibleHeight <= 0.0f) {
                continue;
            }
            const float srcX0 = progress.textureX;
            const float srcY0 = progress.textureY + progress.height - visibleHeight;
            const float srcX1 = progress.textureX + progress.width;
            const float srcY1 = progress.textureY + progress.height;
            const float dstX0 = panelRect.x + progress.x * scale;
            const float dstY0 = panelRect.y + (progress.y + progress.height - visibleHeight) * scale;
            drawTextureQuad(context, backgroundTexture, dstX0, dstY0, dstX0 + progress.width * scale,
                            dstY0 + visibleHeight * scale, srcX0 / def.textureWidth, 1.0f - srcY1 / def.textureHeight,
                            srcX1 / def.textureWidth, 1.0f - srcY0 / def.textureHeight, 1.0f);
        } else if (progress.direction == "right") {
            const float visibleWidth = std::round(progress.width * fraction);
            if (visibleWidth <= 0.0f) {
                continue;
            }
            const float dstX0 = panelRect.x + progress.x * scale;
            const float dstY0 = panelRect.y + progress.y * scale;
            drawTextureQuad(context, backgroundTexture, dstX0, dstY0, dstX0 + visibleWidth * scale,
                            dstY0 + progress.height * scale, progress.textureX / def.textureWidth,
                            1.0f - (progress.textureY + progress.height) / def.textureHeight,
                            (progress.textureX + visibleWidth) / def.textureWidth,
                            1.0f - progress.textureY / def.textureHeight, 1.0f);
        }
    }
}

void DataDrivenContainerPanelControl::drawTextureQuad(const UIRenderContext& context, const RhiTextureHandle texture,
                                                      const float x0, const float y0, const float x1, const float y1,
                                                      const float u0, const float v0, const float u1, const float v1,
                                                      const float opacity) const {
    if (context.commandList == nullptr || context.uiRenderer == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid() || !texture.isValid() || context.screenWidth <= 0 ||
        context.screenHeight <= 0 || x1 <= x0 || y1 <= y0 || opacity <= 0.0f) {
        return;
    }

    const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(texture);
    if (!bindGroup.isValid()) {
        return;
    }

    const float bottomY0 = static_cast<float>(context.screenHeight) - y1;
    const ImageTexturePushConstants pushConstants{
        glm::vec4(static_cast<float>(context.screenWidth), static_cast<float>(context.screenHeight), x0, bottomY0),
        glm::vec4(x1 - x0, y1 - y0, 0.0f, 0.0f), glm::vec4(u0, v0, u1, v1), glm::vec4(1.0f, 1.0f, 1.0f, opacity)};

    RhiCommandList& commandList = *context.commandList;
    commandList.setGraphicsPipeline(context.imageTexturePipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.setBindGroup(0u, bindGroup);
    commandList.setScissor(containerScissor(context));
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(6u, 1u, 0u, 0u);
}

void DataDrivenContainerPanelControl::renderDraggedItem(const UIRenderContext& context) const {
    if (!context.hasDraggedItem || context.draggedItemId <= 0 || m_resourceMgr == nullptr) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resourceMgr->getItemTextureAtlas();
    const auto draggedItem = static_cast<ItemID>(context.draggedItemId);
    const ItemDef& itemDef = ItemRegistry::get(draggedItem);

    const bool useBakedBlockIcon = ui::shouldUseBakedBlockIcon(itemDef);
    const int itemTileIndex = useBakedBlockIcon ? -1 : m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    const TextureAtlas& selectedAtlas = useBakedBlockIcon ? itemIconAtlas : itemTextureAtlas;
    const int selectedTile = useBakedBlockIcon ? static_cast<int>(itemDef.renderBlock) : itemTileIndex;
    if (!selectedAtlas.texture.isValid() || selectedAtlas.tilesPerRow <= 0 || selectedTile < 0) {
        return;
    }

    const ui::ContainerUiDef& def = requireDefinition();
    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    float iconSize = std::max(1.0f, 18.0f * panelRect.scale);
    for (const ui::ContainerSlotGroupDef& group : def.slotGroups) {
        if (group.kind == ui::ContainerSlotGroupKind::Container ||
            group.kind == ui::ContainerSlotGroupKind::PlayerInventory) {
            iconSize = std::max(1.0f, group.slotSize * panelRect.scale);
            break;
        }
    }

    constexpr float kDragCursorOffsetPx = 1.0f;
    const float x0 = context.pointerX + kDragCursorOffsetPx;
    const float topY0 = context.pointerY + kDragCursorOffsetPx;
    const float x1 = x0 + iconSize;
    const float topY1 = topY0 + iconSize;
    const auto uv = selectedAtlas.getUV(selectedTile);
    drawTextureQuad(context, selectedAtlas.texture, x0, topY0, x1, topY1, uv.first.x, uv.first.y, uv.second.x,
                    uv.second.y, 0.95f);
}

void DataDrivenContainerPanelControl::renderTooltip(const UIRenderContext& context) const {
    if (context.hasDraggedItem) {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
        return;
    }

    ItemID hoveredId = m_containerGrid.getHoveredItemId();
    if (hoveredId == 0) {
        hoveredId = m_playerGrid.getHoveredItemId();
    }

    if (hoveredId != 0) {
        const ItemDef& def = ItemRegistry::get(hoveredId);
        const std::string name = context.localeManager ? context.localeManager->getItemName(def.namespacedId.path())
                                                       : std::string(def.namespacedId.path());
        if (hoveredId != m_tooltipHoveredItemId) {
            m_tooltipHoveredItemId = hoveredId;
        }
        m_tooltip.startHover(name, context.pointerX, context.pointerY, static_cast<float>(context.screenWidth),
                             static_cast<float>(context.screenHeight), context.timeSeconds);
    } else {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    }

    m_tooltip.render(context);
}
