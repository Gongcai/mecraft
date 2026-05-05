#include "ItemGridControl.h"

#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

void ItemGridControl::init(ResourceMgr& resourceMgr)
{
    m_resourceMgr = &resourceMgr;
    m_crosshairShader = resourceMgr.getShader("crosshair");
    m_inventoryShader = resourceMgr.getShader("inventory");
    Pickable::initMesh(m_mesh);
}

void ItemGridControl::shutdown()
{
    Pickable::shutdownMesh(m_mesh);
    m_resourceMgr = nullptr;
    m_crosshairShader = nullptr;
    m_inventoryShader = nullptr;
    m_hoveredIndex = -1;
    m_lastActivatedIndex = -1;
}

void ItemGridControl::render(const UIRenderContext& context) const
{
    if (!m_visible || !m_resourceMgr || m_slots.empty()) {
        return;
    }
    if (context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    Pickable::render(m_slots.data(),
                     static_cast<int>(m_slots.size()),
                     m_hoveredIndex,
                     context.screenWidth,
                     context.screenHeight,
                     m_renderParams,
                     m_crosshairShader,
                     m_inventoryShader,
                     m_mesh,
                     *m_resourceMgr,
                     m_resourceMgr->getItemIconAtlas(),
                     m_resourceMgr->getItemTextureAtlas(),
                     context.textRenderer);
}

UIEventResult ItemGridControl::onInput(const UIInputEvent& event)
{
    if (!m_visible || m_slots.empty()) {
        m_hoveredIndex = -1;
        return UIEventResult::Ignored;
    }

    m_hoveredIndex = hitTest(event.x, event.y);
    if (event.type == UIInputEventType::PointerMove) {
        return m_hoveredIndex >= 0 ? UIEventResult::Handled : UIEventResult::Ignored;
    }

    if (event.type == UIInputEventType::PointerDown &&
        event.button == UIPointerButton::Primary &&
        m_hoveredIndex >= 0) {
        m_lastActivatedIndex = m_hoveredIndex;
        return UIEventResult::Consumed;
    }

    return m_hoveredIndex >= 0 ? UIEventResult::Handled : UIEventResult::Ignored;
}

bool ItemGridControl::isVisible() const
{
    return m_visible;
}

void ItemGridControl::setVisible(bool visible)
{
    m_visible = visible;
    if (!m_visible) {
        m_hoveredIndex = -1;
    }
}

void ItemGridControl::setSlots(const Pickable::SlotInfo* slots, int count)
{
    m_slots.clear();
    if (!slots || count <= 0) {
        m_hoveredIndex = -1;
        return;
    }

    m_slots.assign(slots, slots + count);
}

void ItemGridControl::clearSlots()
{
    m_slots.clear();
    m_hoveredIndex = -1;
}

void ItemGridControl::setCountTextOffsetX(float offsetX)
{
    m_renderParams.countTextOffsetX = offsetX;
}

void ItemGridControl::setCountTextOffsetY(float offsetY)
{
    m_renderParams.countTextOffsetY = offsetY;
}

void ItemGridControl::setCountTextScale(float scale)
{
    m_renderParams.countTextScale = std::max(0.1f, scale);
}

const Pickable::RenderParams& ItemGridControl::getRenderParams() const
{
    return m_renderParams;
}

int ItemGridControl::getHoveredIndex() const
{
    return m_hoveredIndex;
}

int ItemGridControl::getLastActivatedIndex() const
{
    return m_lastActivatedIndex;
}

void ItemGridControl::clearLastActivatedIndex()
{
    m_lastActivatedIndex = -1;
}

int ItemGridControl::hitTest(float mouseX, float mouseY) const
{
    if (m_slots.empty()) {
        return -1;
    }
    return Pickable::hitTest(m_slots.data(), static_cast<int>(m_slots.size()), mouseX, mouseY);
}

ItemID ItemGridControl::getHoveredItemId() const
{
    if (m_hoveredIndex < 0 || m_hoveredIndex >= static_cast<int>(m_slots.size())) {
        return 0;
    }
    return static_cast<ItemID>(m_slots[m_hoveredIndex].itemId);
}

