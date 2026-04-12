#pragma once

#include <vector>

#include "IUIControl.h"
#include "Pickable.h"

class Shader;

class ItemGridControl : public IUIControl {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void render(const UIRenderContext& context) const override;
    UIEventResult onInput(const UIInputEvent& event) override;
    [[nodiscard]] bool isVisible() const override;

    void setVisible(bool visible);
    void setSlots(const Pickable::SlotInfo* slots, int count);
    void clearSlots();

    [[nodiscard]] int getHoveredIndex() const;
    [[nodiscard]] int getLastActivatedIndex() const;
    void clearLastActivatedIndex();

private:
    int hitTest(float mouseX, float mouseY) const;

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_crosshairShader = nullptr;
    Shader* m_inventoryShader = nullptr;
    Pickable::MeshHandles m_mesh;
    Pickable::RenderParams m_renderParams;
    std::vector<Pickable::SlotInfo> m_slots;
    bool m_visible = true;
    int m_hoveredIndex = -1;
    int m_lastActivatedIndex = -1;
};

