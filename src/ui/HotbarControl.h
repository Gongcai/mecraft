#pragma once

#include <array>
#include <string>
#include <glad/glad.h>

#include "UIWidget.h"
#include "../world/Block.h"
#include "../item/Item.h"
class Window;
class Inventory;
class ResourceMgr;
class Shader;
class TextRenderer;

class HotbarControl : public UIWidget
{
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setInventorySource(const Inventory* inventory);

    void setBgColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getBgColor() const;

    void setBorderColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getBorderColor() const;

    void setIconTintColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getIconTintColor() const;

    void setCountTextScale(float scale);
    [[nodiscard]] float getCountTextScale() const;

    void setItemNameDisplayDuration(float seconds);
    [[nodiscard]] float getItemNameDisplayDuration() const;

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    void initMesh();
    void cleanupMesh();
    void renderInternal(float screenW, float screenH, const Inventory& inventory, const TextRenderer* textRenderer = nullptr) const;
    void renderItemName(float screenW, float screenH, const Inventory& inventory, const TextRenderer& textRenderer, float timeSeconds) const;
    void checkSlotChange(const Inventory& inventory) const;

    Shader* m_inventoryShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    ResourceMgr* m_resourceMgr = nullptr;
    const Inventory* m_inventory = nullptr;

    std::array<float, 4> m_bgColor {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_borderColor {1.0f, 1.0f, 1.0f, 0.9f};
    std::array<float, 4> m_iconTintColor {1.0f, 1.0f, 1.0f, 1.0f};

    // Count text layout parameters.
    // Actual font scale = countTextScale * slotSize / 8.0 (8 = base glyph pixel size).
    float m_countTextScale = 0.35f;     // Ratio of slot size for text height

    // Item name popup state (mutable to allow updates in const render method)
    mutable int m_lastSelectedSlot = -1;
    mutable ItemID m_lastSelectedItem = 0;
    mutable std::string m_itemName;
    mutable float m_itemNameShowTime = -100.0f;
    float m_itemNameDisplayDuration = 2.0f;
};

