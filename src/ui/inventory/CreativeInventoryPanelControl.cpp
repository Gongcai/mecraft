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
#include "../../resource/GameResources.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderer.h"
#include "../core/UIPrimitives.h"

namespace {
constexpr int kColumns = 9;
constexpr int kCreativeRows = 4;
constexpr int kInventoryRows = 4;
constexpr float kTabHeight = 24.0f;
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
    return context.fullFramebufferScissor();
}

} // namespace

CreativeInventoryPanelControl::CreativeInventoryPanelControl() {
    visible = false;
}

void CreativeInventoryPanelControl::init(GameResources& resources, RhiDevice& rhiDevice) {
    UIWidget::init(resources, rhiDevice);
    m_resources = &resources;

    m_inventoryGrid.init(resources, rhiDevice);
    m_creativeGrid.init(resources, rhiDevice);
    m_hotbarGrid.init(resources, rhiDevice);
    m_tooltip.init(resources, rhiDevice);
}

void CreativeInventoryPanelControl::shutdown() {
    m_tooltip.shutdown();
    m_hotbarGrid.shutdown();
    m_creativeGrid.shutdown();
    m_inventoryGrid.shutdown();
    m_creativeItems.clear();
    m_inventory = nullptr;
    m_resources = nullptr;
    m_lastActivatedSlot = -1;
    m_lastActivatedCreativeItem = 0;
    UIWidget::shutdown();
}

void CreativeInventoryPanelControl::renderSelf(const UIRenderContext& context) const {
    auto* self = const_cast<CreativeInventoryPanelControl*>(this);
    self->m_cachedUiWidth = context.uiWidth;
    self->m_cachedUiHeight = context.uiHeight;
    self->syncSlots();

    const ResolvedPanelRect panelRect = resolvePanelRect(context.uiWidth, context.uiHeight);

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

UIEventResult CreativeInventoryPanelControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) {
        return UIEventResult::Ignored;
    }

    const int uiWidth = ctx.uiWidth > 0 ? ctx.uiWidth : m_cachedUiWidth;
    const int uiHeight = ctx.uiHeight > 0 ? ctx.uiHeight : m_cachedUiHeight;
    m_cachedUiWidth = uiWidth;
    m_cachedUiHeight = uiHeight;
    syncSlots();

    const ResolvedPanelRect panelRect = resolvePanelRect(uiWidth, uiHeight);
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
CreativeInventoryPanelControl::resolvePanelRect(const int uiWidth, const int uiHeight) const {
    const int safeWidth = std::max(1, uiWidth);
    const int safeHeight = std::max(1, uiHeight);
    const float availableWidth = std::max(1.0f, static_cast<float>(safeWidth) - m_layout.fitPadding * 2.0f);
    const float availableHeight = std::max(1.0f, static_cast<float>(safeHeight) - m_layout.fitPadding * 2.0f);
    const float fitScale =
        std::min(availableWidth / m_layout.sourceWidth, availableHeight / (m_layout.sourceHeight + kTabHeight));
    const float scale = std::max(0.1f, std::min(m_layout.panelScale, fitScale));

    ResolvedPanelRect rect;
    rect.scale = scale;
    rect.width = m_layout.sourceWidth * scale;
    rect.height = m_layout.sourceHeight * scale;
    rect.x = (static_cast<float>(safeWidth) - rect.width) * 0.5f;
    rect.y = (static_cast<float>(safeHeight) - rect.height - kTabHeight * scale) * 0.5f;
    return rect;
}

CreativeInventoryPanelControl::HitRect
CreativeInventoryPanelControl::tabRect(const CreativeInventoryTab tab, const ResolvedPanelRect& panelRect) const {
    HitRect rect;
    rect.width = panelRect.width * 0.5f;
    rect.height = kTabHeight * panelRect.scale;
    rect.x = panelRect.x + (tab == CreativeInventoryTab::AllItems ? 0.0f : rect.width);
    rect.y = panelRect.y + panelRect.height;
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
    const ResolvedPanelRect panelRect = resolvePanelRect(m_cachedUiWidth, m_cachedUiHeight);
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
    const int slotSize = std::max(1, step - inset * 2);
    const int baseY =
        static_cast<int>(std::lround(panelRect.y + panelRect.height - kInventoryGridY * panelRect.scale)) - inset -
        slotSize;
    const int hotbarY =
        static_cast<int>(std::lround(panelRect.y + panelRect.height - kInventoryHotbarY * panelRect.scale)) - inset -
        slotSize;

    int outIndex = 0;
    for (int row = 0; row < kInventoryRows; ++row) {
        const int y = (row == 3) ? hotbarY : baseY - row * step;
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
    const int slotSize = std::max(1, step - inset * 2);
    const int baseY =
        static_cast<int>(std::lround(panelRect.y + panelRect.height - m_layout.itemGridY * panelRect.scale)) - inset -
        slotSize;

    for (int row = 0; row < kCreativeRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const int outIndex = row * kColumns + col;
            const int itemIndex = (m_scrollRow + row) * kColumns + col;
            const ItemID itemId = (itemIndex >= 0 && itemIndex < static_cast<int>(m_creativeItems.size()))
                                      ? m_creativeItems[static_cast<size_t>(itemIndex)]
                                      : 0;
            const ItemDef& def = ItemRegistry::get(itemId);
            const int count = itemId != 0 ? std::max(1, static_cast<int>(def.maxStack)) : 0;
            slots[static_cast<size_t>(outIndex)] = {baseX + col * step, baseY - row * step, slotSize,
                                                    static_cast<int>(itemId), count};
        }
    }

    m_creativeGrid.setSlots(slots.data(), static_cast<int>(slots.size()));
}

void CreativeInventoryPanelControl::clampScrollRow() {
    m_scrollRow = std::clamp(m_scrollRow, 0, maxScrollRow());
}

void CreativeInventoryPanelControl::renderBackground(const UIRenderContext& context) const {
    if (context.theme == nullptr) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.uiWidth, context.uiHeight);
    ui::drawPanelSurface(context, panelRect.x, panelRect.y, panelRect.width, panelRect.height);
}

void CreativeInventoryPanelControl::renderPlayerPreview(const UIRenderContext& context,
                                                        const ResolvedPanelRect& panelRect) const {
    if (m_tab != CreativeInventoryTab::PlayerInventory || !context.humanoidRenderer || context.commandList == nullptr ||
        context.uiToFramebufferScaleX() <= 0.0f || context.uiToFramebufferScaleY() <= 0.0f) {
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

void CreativeInventoryPanelControl::renderTabs(const UIRenderContext& context,
                                               const ResolvedPanelRect& panelRect) const {
    if (context.theme == nullptr) {
        return;
    }

    for (const auto tab : {CreativeInventoryTab::AllItems, CreativeInventoryTab::PlayerInventory}) {
        const HitRect rect = tabRect(tab, panelRect);
        const bool selected = m_tab == tab;
        const bool hovered = hitRectContains(rect, context.pointerX, context.pointerY);
        const auto& background = selected  ? context.theme->tabHeaderActive
                                 : hovered ? context.theme->tabHeaderHover
                                           : context.theme->tabHeader;
        ui::drawFramedRect(context, rect.x, rect.y, rect.width, rect.height, background, context.theme->panelBorder,
                           std::max(1.0f, panelRect.scale * 0.5f));
        if (selected) {
            ui::drawSolidRect(context, rect.x, rect.y, rect.width, 2.0f * panelRect.scale, context.theme->tabIndicator);
        }
        const std::string label =
            context.localeManager ? context.localeManager->tr(tab == CreativeInventoryTab::AllItems ? "creative_items"
                                                                                                    : "inventory_title")
            : tab == CreativeInventoryTab::AllItems ? "All items"
                                                    : "Inventory";
        ui::drawText(context, label, rect.x + 8.0f * panelRect.scale, rect.y + 7.0f * panelRect.scale,
                     1.25f * panelRect.scale, context.theme->textPrimary);
    }
}

void CreativeInventoryPanelControl::renderScroller(const UIRenderContext& context,
                                                   const ResolvedPanelRect& panelRect) const {
    if (context.theme == nullptr) {
        return;
    }
    const float x = panelRect.x + m_layout.scrollbarX * panelRect.scale;
    const float y = panelRect.y + panelRect.height - (m_layout.scrollbarY + kScrollTrackHeight) * panelRect.scale;
    ui::drawSolidRect(context, x, y, kScrollerWidth * panelRect.scale, kScrollTrackHeight * panelRect.scale,
                      context.theme->scrollbarTrack);
    const bool enabled = scrollerEnabled();
    const int maxRow = maxScrollRow();
    const float fraction = enabled ? static_cast<float>(m_scrollRow) / static_cast<float>(maxRow) : 0.0f;
    const float travel = kScrollTrackHeight - kScrollerHeight;
    const float thumbY = y + travel * (1.0f - fraction) * panelRect.scale;
    ui::drawSolidRect(context, x + panelRect.scale, thumbY, (kScrollerWidth - 2.0f) * panelRect.scale,
                      kScrollerHeight * panelRect.scale,
                      enabled ? context.theme->scrollbarThumb : context.theme->textDisabled);
}

void CreativeInventoryPanelControl::renderDraggedItem(const UIRenderContext& context) const {
    if (!context.hasDraggedItem || context.draggedItemId <= 0 || !m_resources) {
        return;
    }

    const TextureAtlas& itemIconAtlas = m_resources->uiTextures.blockIconAtlas();
    const TextureAtlas& itemTextureAtlas = m_resources->uiTextures.itemTextureAtlas();

    const auto draggedItem = static_cast<ItemID>(context.draggedItemId);
    const ItemDef& itemDef = ItemRegistry::get(draggedItem);
    const bool useBakedBlockIcon = ui::shouldUseBakedBlockIcon(itemDef);
    const TextureAtlas& atlas = useBakedBlockIcon ? itemIconAtlas : itemTextureAtlas;
    const int tileIndex = useBakedBlockIcon ? static_cast<int>(itemDef.renderBlock)
                                            : m_resources->uiTextures.itemTextureIndex(itemDef.iconTextureName);
    if (!atlas.texture.isValid() || atlas.tilesPerRow <= 0 || tileIndex < 0) {
        return;
    }

    const ResolvedPanelRect panelRect = resolvePanelRect(context.uiWidth, context.uiHeight);
    const float iconSize = std::max(1.0f, m_layout.slotSize * panelRect.scale);
    constexpr float kDragCursorOffsetPx = 1.0f;
    const float x0 = context.pointerX + kDragCursorOffsetPx;
    const float y0 = context.pointerY - iconSize - kDragCursorOffsetPx;
    const auto uv = atlas.getUV(tileIndex);
    renderGuiTextureQuad(context, atlas.texture, x0, y0, iconSize, iconSize, uv.first.x, uv.first.y, uv.second.x,
                         uv.second.y, 0.95f);
}

void CreativeInventoryPanelControl::renderGuiTextureQuad(const UIRenderContext& context, const RhiTextureHandle texture,
                                                         const float x, const float y, const float width,
                                                         const float height, const float u0, const float v0,
                                                         const float u1, const float v1, const float opacity) const {
    if (!texture.isValid() || context.commandList == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid() || context.uiRenderer == nullptr || context.uiWidth <= 0 ||
        context.uiHeight <= 0 || width <= 0.0f || height <= 0.0f || opacity <= 0.0f) {
        return;
    }

    const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(texture);
    if (!bindGroup.isValid()) {
        return;
    }

    const ImageTexturePushConstants pushConstants{
        glm::vec4(static_cast<float>(context.uiWidth), static_cast<float>(context.uiHeight), x, y),
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
