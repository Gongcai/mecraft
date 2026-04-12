#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "CommandInputOverlay.h"
#include "ConsoleDisplayBox.h"
#include "ConsoleOverlay.h"
#include "CrosshairControl.h"
#include "HotbarControl.h"
#include "Pickable.h"
#include "PickableOverlay.h"
#include "TextRenderer.h"

class Window;
class ResourceMgr;
class Inventory;

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window, const Inventory& inventory);
    void renderCommandInputBox(const std::string& text);
    void renderPickable(const Pickable::SlotInfo* slots, int count, float mouseX, float mouseY);

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
    CrosshairControl m_crosshair;

    HotbarControl m_hotbar;
    PickableOverlay m_pickable;
    TextRenderer m_text;
    CommandInputOverlay m_commandInput;
    ConsoleOverlay m_console;

    std::size_t m_consoleMaxLines = 64;
};
