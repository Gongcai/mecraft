#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "../hud/CommandInputOverlay.h"
#include "../widgets/ConsoleDisplayBox.h"
#include "../hud/ConsoleOverlay.h"
#include "../inventory/CreativeInventoryPanelControl.h"
#include "../hud/CrosshairControl.h"
#include "../inventory/HeldItemPreviewControl.h"
#include "../hud/HotbarControl.h"
#include "../hud/HudControl.h"
#include "../inventory/InventoryPanelControl.h"
#include "../hud/Pickable.h"
#include "../font/TextRenderer.h"
#include "UIRenderContext.h"
#include "UITheme.h"

class Window;
class ResourceMgr;
class Inventory;
class LocaleManager;
class CraftingSystem;
class HumanoidRenderer;
struct InputSnapshot;
class UIScene;
class UIWidget;

class UIRenderer
{
public:
    static constexpr float kRefScreenWidth = 1280.0f;
    static constexpr float kRefScreenHeight = 720.0f;

    UIRenderer();
    ~UIRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window,
                const Inventory& inventory,
                const PlayerStatsData& playerStats,
                const HeldItemPreviewMotion& heldItemMotion,
                const InputSnapshot& inputSnapshot);
    void renderCommandInputBox(const std::string& text);
    void renderPickable(const Pickable::SlotInfo* slots, int count, float mouseX, float mouseY);
    [[nodiscard]] UIEventResult routeUIInput(const UIInputEvent& event) const;
    void setInventoryPanelVisible(bool visible);
    void setHumanoidRenderer(HumanoidRenderer* humanoidRenderer);
    void setInventoryPanelLayout(const InventoryPanelLayout& layout);
    [[nodiscard]] const InventoryPanelLayout& getInventoryPanelLayout() const;
    [[nodiscard]] int getInventoryPanelLastActivatedSlot() const;
    [[nodiscard]] int getInventoryPanelHoveredSlot() const;

    void setCreativeInventoryVisible(bool visible);
    void setCreativeInventoryTab(CreativeInventoryTab tab);
    [[nodiscard]] CreativeInventoryTab getCreativeInventoryTab() const;
    [[nodiscard]] int getCreativeInventoryLastActivatedSlot() const;
    [[nodiscard]] ItemID getCreativeInventoryLastActivatedCreativeItem() const;
    [[nodiscard]] int getCreativeInventoryHoveredInventorySlot() const;
    void clearCreativeInventoryActivations();

    // Crafting grid slot access
    [[nodiscard]] int getCraftingGridLastActivatedSlot() const;
    [[nodiscard]] int getCraftingGridHoveredSlot() const;
    [[nodiscard]] CraftingGridControl& getCraftingGrid();
    [[nodiscard]] const CraftingGridControl& getCraftingGrid() const;

    // Crafting system connection
    void setCraftingSystem(const CraftingSystem* craftingSystem);

    void appendCommandLine(const std::string& command);
    void appendOutputLine(const std::string& message,
                          ConsoleDisplayBox::MessageType type = ConsoleDisplayBox::MessageType::Normal);
    void appendWarningLine(const std::string& message);
    void appendSuccessLine(const std::string& message);
    void clearConsoleLines();

    void renderText(const std::string& text,
                    float x,
                    float y,
                    float scale,
                    const std::array<float, 4>& color,
                    float screenWidth,
                    float screenHeight);

    // Scene management for menu screens
    void setActiveScene(UIScene* scene);
    [[nodiscard]] UIScene* getActiveScene() const;
    [[nodiscard]] ResourceMgr* getResourceMgr() const;
    void renderSceneOnly(const Window& window, const InputSnapshot& inputSnapshot);

    void setCommandCaretBlinkPeriodMs(float periodMs);
    [[nodiscard]] float getCommandCaretBlinkPeriodMs() const;

    // Theme
    void setTheme(const UITheme& theme);
    [[nodiscard]] const UITheme& getTheme() const;
    [[nodiscard]] UITheme& getTheme();

    // Locale
    void setLocaleManager(const LocaleManager* localeManager);

    void setCrosshairSize(float size);
    [[nodiscard]] float getCrosshairSize() const;

    void setCrosshairColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getCrosshairColor() const;

    void setHotbarBgColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarBgColor() const;

    void setHotbarBorderColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarBorderColor() const;

    void setHotbarIconTintColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarIconTintColor() const;

    void setHotbarCountTextScale(float scale);
    [[nodiscard]] float getHotbarCountTextScale() const;

    void setInventoryCountTextOffsetX(float offsetX);
    [[nodiscard]] float getInventoryCountTextOffsetX() const;
    void setInventoryCountTextOffsetY(float offsetY);
    [[nodiscard]] float getInventoryCountTextOffsetY() const;
    void setInventoryCountTextScale(float scale);
    [[nodiscard]] float getInventoryCountTextScale() const;

    void setHeldItemPreviewLayout(const HeldItemPreviewLayout& layout);
    [[nodiscard]] const HeldItemPreviewLayout& getHeldItemPreviewLayout() const;
    void triggerHeldItemPreviewActionAnimation();
    void setHeldItemPreviewActionAnimationActive(bool active);
    [[nodiscard]] bool consumeHeldItemPreviewSwingTrigger();
    [[nodiscard]] bool isHeldItemPreviewActionAnimationActive() const;

private:
    [[nodiscard]] UIRenderContext makeContextFromWindow(const Window& window,
                                                        const Inventory& inventory,
                                                        const PlayerStatsData& playerStats,
                                                        const HeldItemPreviewMotion& heldItemMotion,
                                                        const InputSnapshot& inputSnapshot) const;
    [[nodiscard]] UIRenderContext makeContextFromViewport() const;
    void renderControls(const UIRenderContext& context) const;

    CrosshairControl m_crosshair;
    HeldItemPreviewControl m_heldItemPreview;

    HotbarControl m_hotbar;
    HudControl m_hud;
    InventoryPanelControl m_inventoryPanel;
    CreativeInventoryPanelControl m_creativeInventoryPanel;
    TextRenderer m_text;
    CommandInputOverlay m_commandInput;
    ConsoleOverlay m_console;
    std::vector<UIWidget*> m_widgetControls;
    ResourceMgr* m_resourceMgr = nullptr;
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    UIScene* m_activeScene = nullptr;
    UITheme m_theme;
    const LocaleManager* m_localeManager = nullptr;
    mutable UIRenderContext m_lastSceneContext;
    bool m_commandInputRequested = false;
    bool m_heldItemPreviewSwingTriggered = false;
    bool m_heldItemPreviewActionAnimationActive = false;

    std::size_t m_consoleMaxLines = 64;
};
