#include "CraftingGridControl.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../../crafting/CraftingSystem.h"
#include "../../resource/ResourceMgr.h"

void CraftingGridControl::init(ResourceMgr& resourceMgr) {
    m_itemGrid.init(resourceMgr);
}

void CraftingGridControl::shutdown() {
    m_itemGrid.shutdown();
}

void CraftingGridControl::renderSelf(const UIRenderContext& context) const {
    if (!visible) {
        return;
    }

    auto* self = const_cast<CraftingGridControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlotPositions();
    m_itemGrid.render(context);
}

UIEventResult CraftingGridControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible) {
        return UIEventResult::Ignored;
    }
    syncSlotPositions();
    return m_itemGrid.onInput(event, ctx);
}

void CraftingGridControl::setVisible(bool v) {
    visible = v;
    m_itemGrid.setVisible(v);
}

void CraftingGridControl::setLayout(const CraftingGridLayout& layout) {
    m_layout = layout;
    m_layout.gridSize =
        std::clamp(m_layout.gridSize, CraftingGridLayout::MIN_GRID_SIZE, CraftingGridLayout::MAX_GRID_SIZE);
}

const CraftingGridLayout& CraftingGridControl::getLayout() const {
    return m_layout;
}

ItemID CraftingGridControl::getCraftingSlot(int index) const {
    if (index < 0 || index >= getCraftingCellCount()) {
        return 0;
    }
    return m_slots[static_cast<size_t>(index)].itemId;
}

uint16_t CraftingGridControl::getCraftingSlotCount(int index) const {
    if (index < 0 || index >= getCraftingCellCount()) {
        return 0;
    }
    return m_slots[static_cast<size_t>(index)].count;
}

void CraftingGridControl::setCraftingSlot(int index, const ItemID itemId, uint16_t count) {
    if (index < 0 || index >= getCraftingCellCount()) {
        return;
    }
    if (itemId == 0 || count == 0) {
        m_slots[static_cast<size_t>(index)] = {};
        return;
    }
    m_slots[static_cast<size_t>(index)] = {itemId, count, 0};
}

ItemID CraftingGridControl::getResultSlot() const {
    return m_slots[static_cast<size_t>(getResultSlotIndex())].itemId;
}

void CraftingGridControl::setResultSlot(const ItemID itemId, uint16_t count) {
    if (itemId == 0 || count == 0) {
        m_slots[static_cast<size_t>(getResultSlotIndex())] = {};
        return;
    }
    m_slots[static_cast<size_t>(getResultSlotIndex())] = {itemId, count, 0};
}

int CraftingGridControl::getResultCount() const {
    return m_slots[static_cast<size_t>(getResultSlotIndex())].count;
}

int CraftingGridControl::getGridSize() const {
    return std::clamp(m_layout.gridSize, CraftingGridLayout::MIN_GRID_SIZE, CraftingGridLayout::MAX_GRID_SIZE);
}

int CraftingGridControl::getCraftingCellCount() const {
    const int size = getGridSize();
    return size * size;
}

int CraftingGridControl::getResultSlotIndex() const {
    return getCraftingCellCount();
}

bool CraftingGridControl::consumeOneCraft() {
    const int resultIndex = getResultSlotIndex();
    if (m_slots[static_cast<size_t>(resultIndex)].isEmpty()) {
        return false;
    }

    bool consumedAny = false;
    for (int i = 0; i < getCraftingCellCount(); ++i) {
        ItemStack& stack = m_slots[static_cast<size_t>(i)];
        if (stack.isEmpty()) {
            stack = {};
            continue;
        }

        --stack.count;
        consumedAny = true;
        if (stack.count == 0) {
            stack = {};
        }
    }

    m_slots[static_cast<size_t>(resultIndex)] = {};
    return consumedAny;
}

void CraftingGridControl::clearAll() {
    m_slots.fill({});
}

void CraftingGridControl::updateCraftingResult(const CraftingSystem& craftingSystem) {
    const int gridSize = getGridSize();
    const int cellCount = getCraftingCellCount();
    std::vector<ItemID> grid;
    grid.reserve(static_cast<size_t>(cellCount));
    for (int i = 0; i < cellCount; ++i) {
        grid.push_back(m_slots[static_cast<size_t>(i)].isEmpty() ? ItemID{0} : m_slots[static_cast<size_t>(i)].itemId);
    }

    const int resultIndex = getResultSlotIndex();
    CraftingResult result = craftingSystem.match(grid, gridSize, gridSize);
    if (result.matched && result.itemId != 0 && result.count > 0) {
        const int clampedCount = std::min(result.count, static_cast<int>(std::numeric_limits<uint16_t>::max()));
        m_slots[static_cast<size_t>(resultIndex)] = {result.itemId, static_cast<uint16_t>(clampedCount), 0};
    } else {
        m_slots[static_cast<size_t>(resultIndex)] = {};
    }
}

int CraftingGridControl::getLastActivatedSlot() const {
    return m_itemGrid.getLastActivatedIndex();
}

int CraftingGridControl::getHoveredSlot() const {
    return m_itemGrid.getHoveredIndex();
}

void CraftingGridControl::clearActivation() {
    m_itemGrid.clearLastActivatedIndex();
}

void CraftingGridControl::setPanelOrigin(float panelX, float panelY, float scale) {
    m_panelX = panelX;
    m_panelY = panelY;
    m_panelScale = scale;
}

void CraftingGridControl::syncSlotPositions() {
    const float scale = m_panelScale;
    const int baseX = static_cast<int>(std::lround(m_panelX + m_layout.offsetX * scale));
    const int baseY = static_cast<int>(std::lround(m_panelY + m_layout.offsetY * scale));
    const int slotSize = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * scale)));
    const int colStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.columnGap) * scale)));
    const int rowStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.rowGap) * scale)));

    std::array<Pickable::SlotInfo, CraftingGridLayout::MAX_TOTAL_SLOTS> slots{};

    const int gridSize = getGridSize();
    for (int row = 0; row < gridSize; ++row) {
        for (int col = 0; col < gridSize; ++col) {
            const int idx = row * gridSize + col;
            slots[static_cast<size_t>(idx)] = {baseX + col * colStep, baseY + row * rowStep, slotSize,
                                               static_cast<int>(m_slots[static_cast<size_t>(idx)].itemId),
                                               static_cast<int>(m_slots[static_cast<size_t>(idx)].count)};
        }
    }

    const int resultIndex = getResultSlotIndex();
    const int resultX = static_cast<int>(std::lround(m_panelX + m_layout.resultOffsetX * scale));
    const int resultY = static_cast<int>(std::lround(m_panelY + m_layout.resultOffsetY * scale));
    const int resultSize = std::max(1, static_cast<int>(std::lround(m_layout.resultSlotSize * scale)));
    slots[static_cast<size_t>(resultIndex)] = {resultX, resultY, resultSize,
                                               static_cast<int>(m_slots[static_cast<size_t>(resultIndex)].itemId),
                                               static_cast<int>(m_slots[static_cast<size_t>(resultIndex)].count)};

    m_itemGrid.setSlots(slots.data(), resultIndex + 1);
}
