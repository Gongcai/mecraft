#include "UIRenderer.h"

#include <glad/glad.h>

#include "../core/Time.h"
#include "../core/InputManager.h"
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
    m_resourceMgr = &resourceMgr;
    m_crosshair.init(resourceMgr);
    m_text.init(resourceMgr);

    m_hotbar.init(resourceMgr);
    m_hotbar.setVisible(true);
    m_inventoryPanel.init(resourceMgr);
    m_inventoryPanel.setVisible(false);
    m_commandInput.init(resourceMgr);
    m_commandInput.setVisible(false);
    m_console.init(resourceMgr);
    m_console.setVisible(true);
    m_console.setTextRenderer(&m_text);
    m_console.setMaxLines(m_consoleMaxLines);

    m_controls = {
        &m_hotbar,
        &m_inventoryPanel,
        &m_console,
        &m_commandInput,
    };

    for (IUIControl* control : m_controls) {
        m_inputRouter.registerControl(control);
    }
}

void UIRenderer::shutdown()
{
    m_crosshair.shutdown();

    m_console.shutdown();
    m_commandInput.shutdown();
    m_text.shutdown();
    m_inputRouter.clear();
    m_controls.clear();
    m_inventoryPanel.shutdown();
    m_hotbar.shutdown();
    m_commandInputRequested = false;
    m_resourceMgr = nullptr;
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
    m_commandInput.setText(text);
    m_commandInput.setVisible(true);
    m_commandInputRequested = true;
}

void UIRenderer::renderPickable(const Pickable::SlotInfo* slots, int count,
                                float mouseX, float mouseY)
{
    if (!slots || count <= 0 || !m_resourceMgr) {
        return;
    }

    const bool wasVisible = m_inventoryPanel.isVisible();
    m_inventoryPanel.setVisible(true);
    m_inventoryPanel.setSlots(slots, count);
    static_cast<void>(m_inputRouter.route({UIInputEventType::PointerMove, mouseX, mouseY, 0}));
    m_inventoryPanel.render(makeContextFromViewport());
    m_inventoryPanel.setVisible(wasVisible);
}

UIEventResult UIRenderer::routeUIInput(const UIInputEvent& event) const
{
    return m_inputRouter.route(event);
}

void UIRenderer::setInventoryPanelVisible(bool visible)
{
    m_inventoryPanel.setVisible(visible);
}

void UIRenderer::setInventoryPanelLayout(const InventoryPanelLayout& layout)
{
    m_inventoryPanel.setLayout(layout);
}

const InventoryPanelLayout& UIRenderer::getInventoryPanelLayout() const
{
    return m_inventoryPanel.getLayout();
}

int UIRenderer::getInventoryPanelLastActivatedSlot() const
{
    return m_inventoryPanel.itemGrid().getLastActivatedIndex();
}

int UIRenderer::getInventoryPanelHoveredSlot() const
{
    return m_inventoryPanel.itemGrid().getHoveredIndex();
}

int UIRenderer::getCraftingGridLastActivatedSlot() const
{
    return m_inventoryPanel.craftingGrid().getLastActivatedSlot();
}

int UIRenderer::getCraftingGridHoveredSlot() const
{
    return m_inventoryPanel.craftingGrid().getHoveredSlot();
}

CraftingGridControl& UIRenderer::getCraftingGrid()
{
    return m_inventoryPanel.craftingGrid();
}

const CraftingGridControl& UIRenderer::getCraftingGrid() const
{
    return m_inventoryPanel.craftingGrid();
}

void UIRenderer::setCraftingSystem(const CraftingSystem* craftingSystem)
{
    m_inventoryPanel.setCraftingSystem(craftingSystem);
}

void UIRenderer::render(const Window& window, const Inventory& inventory, const InputSnapshot& inputSnapshot)
{
    m_crosshair.render(window);
    m_hotbar.setInventorySource(&inventory);
    m_inventoryPanel.setInventorySource(&inventory);
    m_commandInput.setVisible(m_commandInputRequested);
    renderControls(makeContextFromWindow(window, inventory, inputSnapshot));
    m_commandInputRequested = false;
}

UIRenderContext UIRenderer::makeContextFromWindow(const Window& window,
                                                  const Inventory& inventory,
                                                  const InputSnapshot& inputSnapshot) const
{
    UIRenderContext context;
    context.screenWidth = window.getWidth();
    context.screenHeight = window.getHeight();
    context.timeSeconds = static_cast<float>(Time::getRawTime());
    context.resourceMgr = m_resourceMgr;
    context.inventory = &inventory;
    context.textRenderer = &m_text;
    context.commandInputText = &m_commandInput.getText();
    context.commandInputVisible = m_commandInput.isVisible();
    context.pointerX = inputSnapshot.mousePosition.x;
    context.pointerY = inputSnapshot.mousePosition.y;
    context.hasDraggedItem = inputSnapshot.draggedItem.active;
    context.draggedItemId = inputSnapshot.draggedItem.itemId;
    return context;
}

UIRenderContext UIRenderer::makeContextFromViewport() const
{
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);

    UIRenderContext context;
    context.screenWidth = viewport[2];
    context.screenHeight = viewport[3];
    context.timeSeconds = static_cast<float>(Time::getRawTime());
    context.resourceMgr = m_resourceMgr;
    context.textRenderer = &m_text;
    context.commandInputText = &m_commandInput.getText();
    context.commandInputVisible = m_commandInput.isVisible();
    return context;
}

void UIRenderer::renderControls(const UIRenderContext& context) const
{
    for (const IUIControl* control : m_controls) {
        if (!control || !control->isVisible()) {
            continue;
        }
        control->render(context);
    }
}


