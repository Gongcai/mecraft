#pragma once

#include <vector>

#include "../core/UIWidget.h"
#include "../hud/Pickable.h"
#include "../../item/Item.h"

class ItemGridControl : public UIWidget {
public:
    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

    void setVisible(bool v);
    void setSlots(const Pickable::SlotInfo* slots, int count);
    void clearSlots();

    void setCountTextOffsetX(float offsetX);
    void setCountTextOffsetY(float offsetY);
    void setCountTextScale(float scale);
    [[nodiscard]] const Pickable::RenderParams& getRenderParams() const;

    [[nodiscard]] int getHoveredIndex() const;
    [[nodiscard]] int getLastActivatedIndex() const;
    void clearLastActivatedIndex();
    [[nodiscard]] ItemID getHoveredItemId() const;

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    int hitTest(float mouseX, float mouseY) const;

    GameResources* m_resources = nullptr;
    Pickable::RenderParams m_renderParams;
    std::vector<Pickable::SlotInfo> m_slots;
    int m_hoveredIndex = -1;
    int m_lastActivatedIndex = -1;
};
