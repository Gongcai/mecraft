#include "CraftingGridControl.h"

#include <cmath>
#include <vector>

#include "../crafting/CraftingSystem.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

void CraftingGridControl::init(ResourceMgr& resourceMgr)
{
    m_itemGrid.init(resourceMgr);
}

void CraftingGridControl::shutdown()
{
    m_itemGrid.shutdown();
}

void CraftingGridControl::render(const UIRenderContext& context) const
{
    if (!m_visible) {
        return;
    }

    auto* self = const_cast<CraftingGridControl*>(this);
    self->m_cachedScreenWidth = context.screenWidth;
    self->m_cachedScreenHeight = context.screenHeight;
    self->syncSlotPositions();
    m_itemGrid.render(context);
}

UIEventResult CraftingGridControl::onInput(const UIInputEvent& event)
{
    if (!m_visible) {
        return UIEventResult::Ignored;
    }
    syncSlotPositions();
    return m_itemGrid.onInput(event);
}

bool CraftingGridControl::isVisible() const
{
    return m_visible;
}

void CraftingGridControl::setVisible(bool visible)
{
    m_visible = visible;
    m_itemGrid.setVisible(visible);
}

void CraftingGridControl::setLayout(const CraftingGridLayout& layout)
{
    m_layout = layout;
}

const CraftingGridLayout& CraftingGridControl::getLayout() const
{
    return m_layout;
}

BlockID CraftingGridControl::getCraftingSlot(int index) const
{
    if (index < 0 || index >= CraftingGridLayout::GRID_SIZE * CraftingGridLayout::GRID_SIZE) {
        return BlockType::AIR;
    }
    return m_slots[static_cast<size_t>(index)];
}

void CraftingGridControl::setCraftingSlot(int index, BlockID block)
{
    if (index < 0 || index >= CraftingGridLayout::GRID_SIZE * CraftingGridLayout::GRID_SIZE) {
        return;
    }
    m_slots[static_cast<size_t>(index)] = block;
}

BlockID CraftingGridControl::getResultSlot() const
{
    return m_slots[4];
}

void CraftingGridControl::setResultSlot(BlockID block)
{
    m_slots[4] = block;
}

void CraftingGridControl::clearAll()
{
    m_slots.fill(BlockType::AIR);
}

void CraftingGridControl::updateCraftingResult(const CraftingSystem& craftingSystem)
{
    // Build a 2x2 grid for the crafting system
    std::vector<BlockID> grid = {
        m_slots[0], m_slots[1],
        m_slots[2], m_slots[3]
    };

    CraftingResult result = craftingSystem.match(grid, CraftingGridLayout::GRID_SIZE, CraftingGridLayout::GRID_SIZE);
    if (result.matched) {
        m_slots[4] = result.itemId;
    } else {
        m_slots[4] = BlockType::AIR;
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
                static_cast<int>(m_slots[static_cast<size_t>(idx)])
            };
        }
    }

    // Result slot (index 4)
    const int resultX = static_cast<int>(std::lround(m_panelX + m_layout.resultOffsetX * scale));
    const int resultY = static_cast<int>(std::lround(m_panelY + m_layout.resultOffsetY * scale));
    const int resultSize = std::max(1, static_cast<int>(std::lround(m_layout.resultSlotSize * scale)));
    slots[4] = { resultX, resultY, resultSize, static_cast<int>(m_slots[4]) };

    m_itemGrid.setSlots(slots.data(), static_cast<int>(slots.size()));
}
