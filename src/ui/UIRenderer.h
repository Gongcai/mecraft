#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "CommandInputOverlay.h"
#include "ConsoleDisplayBox.h"
#include "ConsoleOverlay.h"
#include "CrosshairControl.h"
#include "HotbarControl.h"
#include "InventoryPanelControl.h"
#include "Pickable.h"
#include "TextRenderer.h"
#include "UIInputRouter.h"
#include "UIRenderContext.h"

class Window;
class ResourceMgr;
class Inventory;
class CraftingSystem;
 struct InputSnapshot;

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window, const Inventory& inventory, const InputSnapshot& inputSnapshot);
    void renderCommandInputBox(const std::string& text);
    void renderPickable(const Pickable::SlotInfo* slots, int count, float mouseX, float mouseY);
    [[nodiscard]] UIEventResult routeUIInput(const UIInputEvent& event) const;
    void setInventoryPanelVisible(bool visible);
    void setInventoryPanelLayout(const InventoryPanelLayout& layout);
    [[nodiscard]] const InventoryPanelLayout& getInventoryPanelLayout() const;
    [[nodiscard]] int getInventoryPanelLastActivatedSlot() const;
    [[nodiscard]] int getInventoryPanelHoveredSlot() const;

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

    void setTextAdvanceFactor(float factor);
    [[nodiscard]] float getTextAdvanceFactor() const;

    void setCommandCaretBlinkPeriodMs(float periodMs);
    [[nodiscard]] float getCommandCaretBlinkPeriodMs() const;

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

private:
    [[nodiscard]] UIRenderContext makeContextFromWindow(const Window& window,
                                                        const Inventory& inventory,
                                                        const InputSnapshot& inputSnapshot) const;
    [[nodiscard]] UIRenderContext makeContextFromViewport() const;
    void renderControls(const UIRenderContext& context) const;

    CrosshairControl m_crosshair;

    HotbarControl m_hotbar;
    InventoryPanelControl m_inventoryPanel;
    TextRenderer m_text;
    CommandInputOverlay m_commandInput;
    ConsoleOverlay m_console;
    UIInputRouter m_inputRouter;
    std::vector<IUIControl*> m_controls;
    ResourceMgr* m_resourceMgr = nullptr;
    bool m_commandInputRequested = false;

    std::size_t m_consoleMaxLines = 64;
};
