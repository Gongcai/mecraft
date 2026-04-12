#include "UIRenderer.h"

#include "../core/Time.h"
#include "../core/Window.h"
#include "../player/Inventory.h"
#include "../resource/ResourceMgr.h"

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer()
{
    shutdown();
}

void UIRenderer::init(ResourceMgr& resourceMgr)
{
    m_crosshair.init(resourceMgr);

    m_hotbar.init(resourceMgr);
    m_pickable.init(resourceMgr);
    m_text.init(resourceMgr);
    m_commandInput.init(resourceMgr);
    m_console.init(resourceMgr);
    m_console.setMaxLines(m_consoleMaxLines);
}

void UIRenderer::shutdown()
{
    m_crosshair.shutdown();

    m_console.shutdown();
    m_commandInput.shutdown();
    m_text.shutdown();
    m_pickable.shutdown();
    m_hotbar.shutdown();
}

void UIRenderer::setCrosshairSize(float size)
{
    m_crosshair.setSize(size);
}

float UIRenderer::getCrosshairSize() const
{
    return m_crosshair.getSize();
}

void UIRenderer::setCrosshairColor(const std::array<float, 4>& color)
{
    m_crosshair.setColor(color);
}

const std::array<float, 4>& UIRenderer::getCrosshairColor() const
{
    return m_crosshair.getColor();
}

void UIRenderer::setHotbarBgColor(const std::array<float, 4>& color)
{
    m_hotbar.setBgColor(color);
}

const std::array<float, 4>& UIRenderer::getHotbarBgColor() const
{
    return m_hotbar.getBgColor();
}

void UIRenderer::setHotbarBorderColor(const std::array<float, 4>& color)
{
    m_hotbar.setBorderColor(color);
}

const std::array<float, 4>& UIRenderer::getHotbarBorderColor() const
{
    return m_hotbar.getBorderColor();
}

void UIRenderer::setHotbarIconTintColor(const std::array<float, 4>& color)
{
    m_hotbar.setIconTintColor(color);
}

const std::array<float, 4>& UIRenderer::getHotbarIconTintColor() const
{
    return m_hotbar.getIconTintColor();
}

void UIRenderer::setTextAdvanceFactor(float factor)
{
    m_text.setAdvanceFactor(factor);
}

float UIRenderer::getTextAdvanceFactor() const
{
    return m_text.getAdvanceFactor();
}

void UIRenderer::setCommandCaretBlinkPeriodMs(float periodMs)
{
    m_commandInput.setCaretBlinkPeriodMs(periodMs);
}

float UIRenderer::getCommandCaretBlinkPeriodMs() const
{
    return m_commandInput.getCaretBlinkPeriodMs();
}

void UIRenderer::appendCommandLine(const std::string& command)
{
    if (command.empty()) {
        return;
    }
    appendOutputLine("> " + command, ConsoleDisplayBox::MessageType::Normal);
}

void UIRenderer::appendOutputLine(const std::string& message,
                                  ConsoleDisplayBox::MessageType type)
{
    m_console.appendLine(message, Time::getRawTime(), type);
}

void UIRenderer::appendWarningLine(const std::string& message)
{
    appendOutputLine(message, ConsoleDisplayBox::MessageType::Warning);
}

void UIRenderer::appendSuccessLine(const std::string& message)
{
    appendOutputLine(message, ConsoleDisplayBox::MessageType::Success);
}

void UIRenderer::clearConsoleLines()
{
    m_console.clear();
}

void UIRenderer::renderText(const std::string& text,
                            float x,
                            float y,
                            float scale,
                            const std::array<float, 4>& color,
                            float screenWidth,
                            float screenHeight)
{
    m_text.render(text, x, y, scale, color, screenWidth, screenHeight);
}

void UIRenderer::renderCommandInputBox(const std::string& text)
{
    m_commandInput.render(text, m_text);
}

void UIRenderer::renderPickable(const Pickable::SlotInfo* slots, int count,
                                float mouseX, float mouseY)
{
    m_pickable.render(slots, count, mouseX, mouseY);
}

void UIRenderer::render(const Window& window, const Inventory& inventory)
{
    m_crosshair.render(window);
    m_hotbar.render(window, inventory);
    m_console.render(Time::getRawTime(), m_text);
}


