#include "CraftingGridControl.h"

#include <cmath>
#include <vector>

#include "../../crafting/CraftingSystem.h"
#include "../../renderer/core/Shader.h"
#include "../../resource/ResourceMgr.h"

void CraftingGridControl::init(ResourceMgr& resourceMgr)
{
    m_itemGrid.init(resourceMgr);
}

void CraftingGridControl::shutdown()
{
    m_itemGrid.shutdown();
}

void CraftingGridControl::renderSelf(const UIRenderContext& context) const
{
    if (!visible) {
        return;
    }

    auto* self = const_cast<CraftingGridControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlotPositions();
    m_itemGrid.render(context);
}

UIEventResult CraftingGridControl::onInput(const UIInputEvent& event, const UIRenderContext& ctx)
{
    if (!visible) {
        return UIEventResult::Ignored;
    }
    syncSlotPositions();
    return m_itemGrid.onInput(event, ctx);
}

void CraftingGridControl::setVisible(bool v)
{
    visible = v;
    m_itemGrid.setVisible(v);
}

void CraftingGridControl::setLayout(const CraftingGridLayout& layout)
{
    m_layout = layout;
}

const CraftingGridLayout& CraftingGridControl::getLayout() const
{
    return m_layout;
}

ItemID CraftingGridControl::getCraftingSlot(int index) const
{
    if (index < 0 || index >= CraftingGridLayout::GRID_SIZE * CraftingGridLayout::GRID_SIZE) {
        return 0;
    }
    return m_slots[static_cast<size_t>(index)].itemId;
}

uint16_t CraftingGridControl::getCraftingSlotCount(int index) const
{
    if (index < 0 || index >= CraftingGridLayout::GRID_SIZE * CraftingGridLayout::GRID_SIZE) {
        return 0;
    }
    return m_slots[static_cast<size_t>(index)].count;
}

void CraftingGridControl::setCraftingSlot(int index, const ItemID itemId, uint16_t count)
{
    if (index < 0 || index >= CraftingGridLayout::GRID_SIZE * CraftingGridLayout::GRID_SIZE) {
        return;
    }
    m_slots[static_cast<size_t>(index)] = {itemId, count, 0};
}

ItemID CraftingGridControl::getResultSlot() const
{
    return m_slots[4].itemId;
}

void CraftingGridControl::setResultSlot(const ItemID itemId, uint16_t count)
{
    m_slots[4] = {itemId, count, 0};
}

int CraftingGridControl::getResultCount() const
{
    return m_slots[4].count;
}

void CraftingGridControl::clearAll()
{
    m_slots.fill({});
}

void CraftingGridControl::updateCraftingResult(const CraftingSystem& craftingSystem)
{
    // Build a 2x2 grid for the crafting system
    std::vector<ItemID> grid = {
        m_slots[0].itemId, m_slots[1].itemId,
        m_slots[2].itemId, m_slots[3].itemId
    };

    CraftingResult result = craftingSystem.match(grid, CraftingGridLayout::GRID_SIZE, CraftingGridLayout::GRID_SIZE);
    if (result.matched) {
        m_slots[4] = {result.itemId, static_cast<uint16_t>(result.count), 0};
    } else {
        m_slots[4] = {};
    }
}

int CraftingGridControl::getLastActivatedSlot() const
{
    return m_itemGrid.getLastActivatedIndex();
}

int CraftingGridControl::getHoveredSlot() const
{
    return m_itemGrid.getHoveredIndex();
}

void CraftingGridControl::clearActivation()
{
    m_itemGrid.clearLastActivatedIndex();
}

void CraftingGridControl::setPanelOrigin(float x, float y, float scale)
{
    m_panelX = x;
    m_panelY = y;
    m_panelScale = scale;
}

void CraftingGridControl::syncSlotPositions()
{
    const float scale = m_panelScale;
    const int baseX = static_cast<int>(std::lround(m_panelX + m_layout.offsetX * scale));
    const int baseY = static_cast<int>(std::lround(m_panelY + m_layout.offsetY * scale));
    const int slotSize = std::max(1, static_cast<int>(std::lround(m_layout.slotSize * scale)));
    const int colStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.columnGap) * scale)));
    const int rowStep = std::max(1, static_cast<int>(std::lround((m_layout.slotSize + m_layout.rowGap) * scale)));

    std::array<Pickable::SlotInfo, CraftingGridLayout::TOTAL_SLOTS> slots{};

    // 2x2 crafting grid slots (0-3)
    for (int row = 0; row < CraftingGridLayout::GRID_SIZE; ++row) {
        for (int col = 0; col < CraftingGridLayout::GRID_SIZE; ++col) {
            const int idx = row * CraftingGridLayout::GRID_SIZE + col;
            slots[static_cast<size_t>(idx)] = {
                baseX + col * colStep,
                baseY + row * rowStep,
                slotSize,
                static_cast<int>(m_slots[static_cast<size_t>(idx)].itemId),
                static_cast<int>(m_slots[static_cast<size_t>(idx)].count)
            };
        }
    }

    // Result slot (index 4)
    const int resultX = static_cast<int>(std::lround(m_panelX + m_layout.resultOffsetX * scale));
    const int resultY = static_cast<int>(std::lround(m_panelY + m_layout.resultOffsetY * scale));
    const int resultSize = std::max(1, static_cast<int>(std::lround(m_layout.resultSlotSize * scale)));
    slots[4] = { resultX, resultY, resultSize, static_cast<int>(m_slots[4].itemId), static_cast<int>(m_slots[4].count) };

    m_itemGrid.setSlots(slots.data(), static_cast<int>(slots.size()));
}
