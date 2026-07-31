#include "CreativeInventoryPanelControl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <glm/vec4.hpp>

#include "../../item/Item.h"
#include "../../locale/LocaleManager.h"
#include "../../player/Inventory.h"
#include "../../renderer/renderers/HumanoidRenderer.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/ResourceMgr.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderer.h"

namespace {
constexpr int kColumns = 9;
constexpr int kCreativeRows = 4;
constexpr int kInventoryRows = 4;
constexpr int kTabCount = 7;
constexpr float kTabWidth = 26.0f;
constexpr float kTabHeight = 32.0f;
constexpr float kScrollerWidth = 12.0f;
constexpr float kScrollerHeight = 15.0f;
constexpr float kScrollTrackHeight = 90.0f;
constexpr float kInventoryGridX = 8.0f;
constexpr float kInventoryGridY = 53.0f;
constexpr float kInventoryHotbarY = 111.0f;

struct ImageTexturePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

static_assert(sizeof(ImageTexturePushConstants) == 64u);

[[nodiscard]] RhiRect2D creativeInventoryScissor(const UIRenderContext& context) {
    if (context.hasScissor) {
        return context.scissor;
    }
    return {0, 0,
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenWidth) * context.pixelScale()))),
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenHeight) * context.pixelScale())))};
}

std::string tabTextureName(const bool top, const bool selected, const int index) {
    std::string name = top ? "creative_tab_top_" : "creative_tab_bottom_";
    name += selected ? "selected_" : "unselected_";
    name += std::to_string(index);
    return name;
}
} // namespace

CreativeInventoryPanelControl::CreativeInventoryPanelControl() {
    visible = false;
}

void CreativeInventoryPanelControl::init(ResourceMgr& resourceMgr) {
    UIWidget::init(resourceMgr);
    m_resourceMgr = &resourceMgr;

    m_inventoryGrid.init(resourceMgr);
    m_creativeGrid.init(resourceMgr);
    m_hotbarGrid.init(resourceMgr);
    m_tooltip.init(resourceMgr);
}

void CreativeInventoryPanelControl::shutdown() {
    m_tooltip.shutdown();
    m_hotbarGrid.shutdown();
    m_creativeGrid.shutdown();
    m_inventoryGrid.shutdown();
    m_creativeItems.clear();
    m_inventory = nullptr;
    m_resourceMgr = nullptr;
    m_lastActivatedSlot = -1;
    m_lastActivatedCreativeItem = 0;
    UIWidget::shutdown();
}

void CreativeInventoryPanelControl::renderSelf(const UIRenderContext& context) const {
    auto* self = const_cast<CreativeInventoryPanelControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlots();

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);

    renderTabs(context, panelRect);
    renderBackground(context);
    if (m_tab == CreativeInventoryTab::AllItems) {
        renderScroller(context, panelRect);
        m_creativeGrid.render(context);
        m_hotbarGrid.render(context);
    } else {
        renderPlayerPreview(context, panelRect);
        m_inventoryGrid.render(context);
    }
    renderDraggedItem(context);

    if (context.hasDraggedItem) {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    } else {
        ItemID hoveredId = 0;
        if (m_tab == CreativeInventoryTab::AllItems) {
            hoveredId = m_creativeGrid.getHoveredItemId();
            if (hoveredId == 0) {
                hoveredId = m_hotbarGrid.getHoveredItemId();
            }
        } else {
            hoveredId = m_inventoryGrid.getHoveredItemId();
        }

        if (hoveredId != 0) {
            const ItemDef& def = ItemRegistry::get(hoveredId);
            const std::string name = context.localeManager ? context.localeManager->getItemName(def.namespacedId.path())
                                                           : std::string(def.namespacedId.path());
            if (hoveredId != m_tooltipHoveredItemId || m_tooltip.isHovering()) {
                m_tooltipHoveredItemId = hoveredId;
                m_tooltip.startHover(name, context.pointerX, context.pointerY, static_cast<float>(context.screenWidth),
                                     static_cast<float>(context.screenHeight), context.timeSeconds);
            }
        } else {
            m_tooltip.cancelHover();
            m_tooltipHoveredItemId = 0;
        }
    }
    m_tooltip.render(context);
}

UIEventResult CreativeInventoryPanelControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) {
        return UIEventResult::Ignored;
    }

    const int screenWidth = ctx.screenWidth > 0 ? ctx.screenWidth : m_cachedScreenWidth;
    const int screenHeight = ctx.screenHeight > 0 ? ctx.screenHeight : m_cachedScreenHeight;
    m_cachedScreenWidth = screenWidth;
    m_cachedScreenHeight = screenHeight;
    syncSlots();

    const ResolvedPanelRect panelRect = resolvePanelRect(screenWidth, screenHeight);
    const HitRect playerTab = tabRect(CreativeInventoryTab::PlayerInventory, panelRect);
    const HitRect itemsTab = tabRect(CreativeInventoryTab::AllItems, panelRect);
    const bool overPlayerTab = hitRectContains(playerTab, event.x, event.y);
    const bool overItemsTab = hitRectContains(itemsTab, event.x, event.y);

    if (event.type == UIInputEventType::PointerDown && event.button == UIPointerButton::Primary) {
        if (overPlayerTab) {
            setTab(CreativeInventoryTab::PlayerInventory);
            return UIEventResult::Consumed;
        }
        if (overItemsTab) {
            setTab(CreativeInventoryTab::AllItems);
            return UIEventResult::Consumed;
        }
    }

    if (event.type == UIInputEventType::PointerMove && (overPlayerTab || overItemsTab)) {
        return UIEventResult::Handled;
    }

    if (m_tab == CreativeInventoryTab::AllItems && event.type == UIInputEventType::Scroll) {
        if (!scrollerEnabled()) {
            return UIEventResult::Handled;
        }
        if (event.scrollY > 0.0f) {
            --m_scrollRow;
        } else if (event.scrollY < 0.0f) {
            ++m_scrollRow;
        }
        clampScrollRow();
        syncCreativeSlots(panelRect);
        return UIEventResult::Consumed;
    }

    if (m_tab == CreativeInventoryTab::AllItems) {
        const UIEventResult hotbarResult = m_hotbarGrid.onInput(event, {});
        if (hotbarResult == UIEventResult::Consumed) {
            const int activatedHotbar = m_hotbarGrid.getLastActivatedIndex();
            m_lastActivatedSlot = activatedHotbar >= 0
                                      ? Inventory::MAIN_INVENTORY_ROWS * Inventory::INVENTORY_COLUMNS + activatedHotbar
                                      : -1;
            m_lastActivatedCreativeItem = 0;
            m_creativeGrid.clearLastActivatedIndex();
            return hotbarResult;
        }
        if (hotbarResult == UIEventResult::Handled) {
            return hotbarResult;
        }

        const UIEventResult result = m_creativeGrid.onInput(event, {});
        if (result == UIEventResult::Consumed) {
            const int activated = m_creativeGrid.getLastActivatedIndex();
            const int itemIndex = m_scrollRow * kColumns + activated;
            if (activated >= 0 && itemIndex >= 0 && itemIndex < static_cast<int>(m_creativeItems.size())) {
                m_lastActivatedCreativeItem = m_creativeItems[static_cast<size_t>(itemIndex)];
            } else {
                m_lastActivatedCreativeItem = 0;
            }
            m_lastActivatedSlot = -1;
        }
        return result;
    }

    const UIEventResult result = m_inventoryGrid.onInput(event, {});
    if (result == UIEventResult::Consumed) {
        m_lastActivatedSlot = m_inventoryGrid.getLastActivatedIndex();
    }
    return result;
}

void CreativeInventoryPanelControl::setVisible(const bool isVisible) {
    visible = isVisible;
    m_inventoryGrid.setVisible(isVisible && m_tab == CreativeInventoryTab::PlayerInventory);
    m_creativeGrid.setVisible(isVisible && m_tab == CreativeInventoryTab::AllItems);
    m_hotbarGrid.setVisible(isVisible && m_tab == CreativeInventoryTab::AllItems);
    if (!visible) {
        m_tooltip.cancelHover();
        m_tooltipHoveredItemId = 0;
    }
}

void CreativeInventoryPanelControl::setInventorySource(const Inventory* inventory) {
    m_inventory = inventory;
}

void CreativeInventoryPanelControl::setTab(const CreativeInventoryTab tab) {
    m_tab = tab;
    m_inventoryGrid.setVisible(visible && m_tab == CreativeInventoryTab::PlayerInventory);
    m_creativeGrid.setVisible(visible && m_tab == CreativeInventoryTab::AllItems);
    m_hotbarGrid.setVisible(visible && m_tab == CreativeInventoryTab::AllItems);
    clearActivations();
    clampScrollRow();
}

CreativeInventoryTab CreativeInventoryPanelControl::getTab() const {
    return m_tab;
}

void CreativeInventoryPanelControl::setLayout(const CreativeInventoryLayout& layout) {
    m_layout = layout;
    syncSlots();
}

const CreativeInventoryLayout& CreativeInventoryPanelControl::getLayout() const {
    return m_layout;
}

int CreativeInventoryPanelControl::getLastActivatedSlot() const {
    return m_lastActivatedSlot;
}

int CreativeInventoryPanelControl::getHoveredInventorySlot() const {
    if (m_tab == CreativeInventoryTab::PlayerInventory) {
        return m_inventoryGrid.getHoveredIndex();
    }
    if (m_tab == CreativeInventoryTab::AllItems) {
        const int hoveredHotbar = m_hotbarGrid.getHoveredIndex();
        return hoveredHotbar >= 0 ? Inventory::MAIN_INVENTORY_ROWS * Inventory::INVENTORY_COLUMNS + hoveredHotbar : -1;
    }
    return -1;
}

ItemID CreativeInventoryPanelControl::getLastActivatedCreativeItem() const {
    return m_lastActivatedCreativeItem;
}

void CreativeInventoryPanelControl::clearActivations() {
    m_lastActivatedSlot = -1;
    m_lastActivatedCreativeItem = 0;
    m_inventoryGrid.clearLastActivatedIndex();
    m_creativeGrid.clearLastActivatedIndex();
    m_hotbarGrid.clearLastActivatedIndex();
}

void CreativeInventoryPanelControl::setCreativeItemsForTest(const ItemID* itemIds, const int count) {
    m_useTestCreativeItems = true;
    m_creativeItems.clear();
    if (itemIds && count > 0) {
        m_creativeItems.assign(itemIds, itemIds + count);
    }
    clampScrollRow();
    syncSlots();
}

int CreativeInventoryPanelControl::getScrollRowForTest() const {
    return m_scrollRow;
}

bool CreativeInventoryPanelControl::isScrollerEnabledForTest() const {
    return scrollerEnabled();
}

CreativeInventoryPanelControl::ResolvedPanelRect
CreativeInventoryPanelControl::resolvePanelRect(const int screenWidth, const int screenHeight) const {
    const int safeWidth = std::max(1, screenWidth);
    const int safeHeight = std::max(1, screenHeight);
    const float scale = std::max(0.1f, m_layout.panelScale);

    ResolvedPanelRect rect;
    rect.scale = scale;
    rect.width = m_layout.sourceWidth * scale;
    rect.height = m_layout.sourceHeight * scale;
    rect.x = (static_cast<float>(safeWidth) - rect.width) * 0.5f;
    rect.y = (static_cast<float>(safeHeight) - rect.height) * 0.5f;
    return rect;
}

CreativeInventoryPanelControl::HitRect
CreativeInventoryPanelControl::tabRect(const CreativeInventoryTab tab, const ResolvedPanelRect& panelRect) const {
    const bool top = (tab == CreativeInventoryTab::AllItems);
    const int index = (tab == CreativeInventoryTab::AllItems) ? 1 : 7;
    HitRect rect;
    rect.x = panelRect.x + static_cast<float>(index - 1) * kTabWidth * panelRect.scale;
    rect.y = top ? panelRect.y - kTabHeight * panelRect.scale : panelRect.y + panelRect.height;
    rect.width = kTabWidth * panelRect.scale;
    rect.height = kTabHeight * panelRect.scale;
    return rect;
}

bool CreativeInventoryPanelControl::hitRectContains(const HitRect& rect, const float x, const float y) const {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

int CreativeInventoryPanelControl::maxScrollRow() const {
    ensureCreativeItems();
    const int rows = static_cast<int>((m_creativeItems.size() + kColumns - 1) / kColumns);
    return std::max(0, rows - kCreativeRows);
}

bool CreativeInventoryPanelControl::scrollerEnabled() const {
    return maxScrollRow() > 0;
}

void CreativeInventoryPanelControl::ensureCreativeItems() const {
    if (m_useTestCreativeItems || !m_creativeItems.empty()) {
        return;
    }

    auto* self = const_cast<CreativeInventoryPanelControl*>(this);
    const size_t count = ItemRegistry::getItemCount();
    self->m_creativeItems.reserve(count);
    for (size_t i = 1; i < count; ++i) {
        const ItemID itemId = static_cast<ItemID>(i);
        const ItemDef& def = ItemRegistry::get(itemId);
        if (def.maxStack == 0) {
            continue;
        }
        self->m_creativeItems.push_back(itemId);
    }
}

void CreativeInventoryPanelControl::syncSlots() {
    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedScreenWidth, m_cachedScreenHeight);
    ensureCreativeItems();
    clampScrollRow();
    if (m_tab == CreativeInventoryTab::AllItems) {
        syncCreativeSlots(panelRect);
        syncInventorySlots(panelRect);
    } else {
        syncInventorySlots(panelRect);
    }
}

void CreativeInventoryPanelControl::syncInventorySlots(const ResolvedPanelRect& panelRect) {
    if (!m_inventory) {
        m_inventoryGrid.clearSlots();
        m_hotbarGrid.clearSlots();
        return;
    }

    std::array<Pickable::SlotInfo, Inventory::INVENTORY_SIZE> slots{};
    std::array<Pickable::SlotInfo, Inventory::HOTBAR_SIZE> hotbarSlots{};
    const int step = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * panelRect.scale)));
    const int inset = std::max(1, static_cast<int>(std::lround(1.0f * panelRect.scale)));
    const int baseX = static_cast<int>(std::lround(panelRect.x + kInventoryGridX * panelRect.scale)) + inset;
    const int baseY = static_cast<int>(std::lround(panelRect.y + kInventoryGridY * panelRect.scale)) + inset;
    const int hotbarY = static_cast<int>(std::lround(panelRect.y + kInventoryHotbarY * panelRect.scale)) + inset;
    const int slotSize = std::max(1, step - inset * 2);

    int outIndex = 0;
    for (int row = 0; row < kInventoryRows; ++row) {
        const int y = (row == 3) ? hotbarY : baseY + row * step;
        for (int col = 0; col < kColumns; ++col) {
            const int inventoryIndex = Inventory::toInventoryIndex(row, col);
            const ItemStack stack = m_inventory->getSlotStack(inventoryIndex);
            slots[static_cast<size_t>(outIndex)] = {baseX + col * step, y, slotSize, static_cast<int>(stack.itemId),
                                                    static_cast<int>(stack.count)};
            if (row == 3) {
                hotbarSlots[static_cast<size_t>(col)] = slots[static_cast<size_t>(outIndex)];
            }
            ++outIndex;
        }
    }

    m_inventoryGrid.setSlots(slots.data(), static_cast<int>(slots.size()));
    m_hotbarGrid.setSlots(hotbarSlots.data(), static_cast<int>(hotbarSlots.size()));
}

void CreativeInventoryPanelControl::syncCreativeSlots(const ResolvedPanelRect& panelRect) {
    std::array<Pickable::SlotInfo, kColumns * kCreativeRows> slots{};
    const int step = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * panelRect.scale)));
    const int inset = std::max(1, static_cast<int>(std::lround(1.0f * panelRect.scale)));
    const int baseX = static_cast<int>(std::lround(panelRect.x + m_layout.itemGridX * panelRect.scale)) + inset;
    const int baseY = static_cast<int>(std::lround(panelRect.y + m_layout.itemGridY * panelRect.scale)) + inset;
    const int slotSize = std::max(1, step - inset * 2);

    for (int row = 0; row < kCreativeRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const int outIndex = row * kColumns + col;
            const int itemIndex = (m_scrollRow + row) * kColumns + col;
            const ItemID itemId = (itemIndex >= 0 && itemIndex < static_cast<int>(m_creativeItems.size()))
                                      ? m_creativeItems[static_cast<size_t>(itemIndex)]
                                      : 0;
            const ItemDef& def = ItemRegistry::get(itemId);
            const int count = itemId != 0 ? std::max(1, static_cast<int>(def.maxStack)) : 0;
            slots[static_cast<size_t>(outIndex)] = {baseX + col * step, baseY + row * step, slotSize,
                                                    static_cast<int>(itemId), count};
        }
    }

    m_creativeGrid.setSlots(slots.data(), static_cast<int>(slots.size()));
}

void CreativeInventoryPanelControl::clampScrollRow() {
    m_scrollRow = std::clamp(m_scrollRow, 0, maxScrollRow());
}

void CreativeInventoryPanelControl::renderBackground(const UIRenderContext& context) const {
    if (!m_resourceMgr) {
        return;
    }

    const RhiTextureHandle texture = m_resourceMgr->getGuiTextureHandle(
        m_tab == CreativeInventoryTab::AllItems ? "creative_tab_items" : "creative_tab_inventory");
    if (!texture.isValid()) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    constexpr float kAtlasSize = 256.0f;
    renderGuiTextureQuad(context, texture, panelRect.x, panelRect.y, panelRect.width, panelRect.height, 0.0f,
                         1.0f - (136.0f / kAtlasSize), 195.0f / kAtlasSize, 1.0f, 1.0f);
}

void CreativeInventoryPanelControl::renderPlayerPreview(const UIRenderContext& context,
                                                        const ResolvedPanelRect& panelRect) const {
    if (m_tab != CreativeInventoryTab::PlayerInventory || !context.humanoidRenderer || context.commandList == nullptr ||
        context.pixelScale() <= 0.0f) {
        return;
    }

    const float previewWidth = std::max(1.0f, (m_layout.playerPreviewX1 - m_layout.playerPreviewX0) * panelRect.scale);
    const float previewHeight = std::max(1.0f, (m_layout.playerPreviewY1 - m_layout.playerPreviewY0) * panelRect.scale);
    const float previewX = panelRect.x + m_layout.playerPreviewX0 * panelRect.scale;
    const float previewTopY = panelRect.y + m_layout.playerPreviewY0 * panelRect.scale;
    const float previewY = static_cast<float>(context.screenHeight) - (previewTopY + previewHeight);
    const float pointerBottomY = static_cast<float>(context.screenHeight) - context.pointerY;

    context.humanoidRenderer->renderInventoryPreview(
        *context.commandList, previewX, previewY, previewWidth, previewHeight, context.pixelScale(), context.pointerX,
        pointerBottomY, context.timeSeconds, context.screenWidth, context.screenHeight);
}

void CreativeInventoryPanelControl::renderTabs(const UIRenderContext& context,
                                               const ResolvedPanelRect& panelRect) const {
    if (!m_resourceMgr) {
        return;
    }

    for (int i = 1; i <= kTabCount; ++i) {
        const bool top = true;
        const bool selected = (m_tab == CreativeInventoryTab::AllItems && i == 1);
        const RhiTextureHandle texture = m_resourceMgr->getGuiTextureHandle(tabTextureName(top, selected, i));
        if (!texture.isValid()) {
            continue;
        }
        renderGuiTextureQuad(context, texture, panelRect.x + static_cast<float>(i - 1) * kTabWidth * panelRect.scale,
                             panelRect.y - kTabHeight * panelRect.scale, kTabWidth * panelRect.scale,
                             kTabHeight * panelRect.scale, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    }

    for (int i = 1; i <= kTabCount; ++i) {
        const bool top = false;
        const bool selected = (m_tab == CreativeInventoryTab::PlayerInventory && i == kTabCount);
        const RhiTextureHandle texture = m_resourceMgr->getGuiTextureHandle(tabTextureName(top, selected, i));
        if (!texture.isValid()) {
            continue;
        }
        renderGuiTextureQuad(context, texture, panelRect.x + static_cast<float>(i - 1) * kTabWidth * panelRect.scale,
                             panelRect.y + panelRect.height, kTabWidth * panelRect.scale, kTabHeight * panelRect.scale,
                             0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    }
}

void CreativeInventoryPanelControl::renderScroller(const UIRenderContext& context,
                                                   const ResolvedPanelRect& panelRect) const {
    if (!m_resourceMgr) {
        return;
    }

    const bool enabled = scrollerEnabled();
    const RhiTextureHandle texture =
        m_resourceMgr->getGuiTextureHandle(enabled ? "creative_scroller" : "creative_scroller_disabled");
    if (!texture.isValid()) {
        return;
    }

    const int maxRow = maxScrollRow();
    const float t = (enabled && maxRow > 0) ? static_cast<float>(m_scrollRow) / static_cast<float>(maxRow) : 0.0f;
    const float travel = std::max(0.0f, kScrollTrackHeight - kScrollerHeight);
    renderGuiTextureQuad(context, texture, panelRect.x + m_layout.scrollbarX * panelRect.scale,
                         panelRect.y + (m_layout.scrollbarY + travel * t) * panelRect.scale,
                         kScrollerWidth * panelRect.scale, kScrollerHeight * panelRect.scale, 0.0f, 0.0f, 1.0f, 1.0f,
                         1.0f);
}

void CreativeInventoryPanelControl::renderDraggedItem(const UIRenderContext& context) const {
    if (!context.hasDraggedItem || context.draggedItemId <= 0 || !m_resourceMgr) {
        return;
    }

    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resourceMgr->getItemTextureAtlas();

    const auto draggedItem = static_cast<ItemID>(context.draggedItemId);
    const ItemDef& itemDef = ItemRegistry::get(draggedItem);
    const bool useBakedBlockIcon = ui::shouldUseBakedBlockIcon(itemDef);
    const TextureAtlas& atlas = useBakedBlockIcon ? itemIconAtlas : itemTextureAtlas;
    const int tileIndex = useBakedBlockIcon ? static_cast<int>(itemDef.renderBlock)
                                            : m_resourceMgr->getItemTextureIndex(itemDef.iconTextureName);
    if (!atlas.texture.isValid() || atlas.tilesPerRow <= 0 || tileIndex < 0) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.screenWidth, context.screenHeight);
    const float iconSize = std::max(1.0f, m_layout.slotSize * panelRect.scale);
    constexpr float kDragCursorOffsetPx = 1.0f;
    const float x0 = context.pointerX + kDragCursorOffsetPx;
    const float topY0 = context.pointerY + kDragCursorOffsetPx;
    const auto uv = atlas.getUV(tileIndex);
    renderGuiTextureQuad(context, atlas.texture, x0, topY0, iconSize, iconSize, uv.first.x, uv.first.y, uv.second.x,
                         uv.second.y, 0.95f);
}

void CreativeInventoryPanelControl::renderGuiTextureQuad(const UIRenderContext& context, const RhiTextureHandle texture,
                                                         const float x, const float y, const float width,
                                                         const float height, const float u0, const float v0,
                                                         const float u1, const float v1, const float opacity) const {
    if (!texture.isValid() || context.commandList == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid() || context.uiRenderer == nullptr || context.screenWidth <= 0 ||
        context.screenHeight <= 0 || width <= 0.0f || height <= 0.0f || opacity <= 0.0f) {
        return;
    }

    const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(texture);
    if (!bindGroup.isValid()) {
        return;
    }

    const float bottomY = static_cast<float>(context.screenHeight) - (y + height);
    const ImageTexturePushConstants pushConstants{
        glm::vec4(static_cast<float>(context.screenWidth), static_cast<float>(context.screenHeight), x, bottomY),
        glm::vec4(width, height, 0.0f, 0.0f), glm::vec4(u0, v0, u1, v1), glm::vec4(1.0f, 1.0f, 1.0f, opacity)};

    RhiCommandList& commandList = *context.commandList;
    commandList.setGraphicsPipeline(context.imageTexturePipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.setBindGroup(0u, bindGroup);
    commandList.setScissor(creativeInventoryScissor(context));
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(6u, 1u, 0u, 0u);
}
