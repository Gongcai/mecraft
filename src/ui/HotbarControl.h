#pragma once

#include <array>
#include <string>
#include <glad/glad.h>

#include "UIWidget.h"
#include "../world/block/Block.h"
#include "../item/Item.h"
class Window;
class Inventory;
class ResourceMgr;
class Shader;
class TextRenderer;
class LocaleManager;

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
    void renderCountText(float screenW, float screenH, const int* slotCounts, int slotCount,
                         float slotStride, float startX, float startY, const TextRenderer& textRenderer) const;
    void renderItemName(float screenW, float screenH, const Inventory& inventory, const TextRenderer& textRenderer, float timeSeconds) const;
    void checkSlotChange(const Inventory& inventory, const LocaleManager* localeManager = nullptr) const;

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

    // Dirty flag: skip vertex rebuild when inventory hasn't changed.
    static constexpr int kHotbarSlots = 10;
    mutable bool m_dirty = true;
    mutable int m_cachedVertCount = 0;
    mutable int m_cachedSelectedSlot = -1;
    mutable int m_cachedSlotCounts[kHotbarSlots] = {};
    mutable ItemID m_cachedSlotItems[kHotbarSlots] = {};
    mutable float m_cachedScreenW = 0.0f;
    mutable float m_cachedScreenH = 0.0f;

    // Cached draw state for replaying without vertex rebuild.
    mutable int m_cachedBgVertCount = 0;
    mutable int m_cachedSelectedVertCount = 0;
    mutable GLuint m_cachedBgTexture = 0;
    mutable int m_cachedIconVertCounts[3] = {};
    mutable GLuint m_cachedIconTextures[3] = {};

    // Cached layout position for text rendering in cache-hit path.
    mutable float m_cachedStartX = 0.0f;
    mutable float m_cachedStartY = 0.0f;
};

