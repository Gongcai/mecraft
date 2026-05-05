#include "UIRenderer.h"

#include <algorithm>

#include <glad/glad.h>

#include "../core/Time.h"
#include "../core/InputManager.h"
#include "../core/Window.h"
#include "../player/Inventory.h"
#include "../resource/ResourceMgr.h"
#include "UIScene.h"

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

    m_heldItemPreview.init(resourceMgr);
    m_heldItemPreview.visible = true;
    m_hotbar.init(resourceMgr);
    m_hotbar.visible = true;
    m_hud.init(resourceMgr);
    m_hud.visible = true;
    m_inventoryPanel.init(resourceMgr);
    m_inventoryPanel.visible = false;
    m_commandInput.init(resourceMgr);
    m_commandInput.visible = false;
    m_console.init(resourceMgr);
    m_console.visible = true;
    m_console.setTextRenderer(&m_text);
    m_console.setMaxLines(m_consoleMaxLines);

    m_widgetControls = {
        &m_hud,
        &m_console,
        &m_commandInput,
        &m_hotbar,
        &m_inventoryPanel,
        &m_heldItemPreview,
    };

    m_lastSceneContext = {};
    m_lastSceneContext.resourceMgr = m_resourceMgr;
    m_lastSceneContext.textRenderer = &m_text;
}

void UIRenderer::shutdown()
{
    m_crosshair.shutdown();

    m_console.shutdown();
    m_commandInput.shutdown();
    m_text.shutdown();
    m_inventoryPanel.shutdown();
    m_hud.shutdown();
    m_hotbar.shutdown();
    m_heldItemPreview.shutdown();
    m_commandInputRequested = false;
    m_lastSceneContext = {};
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


void UIRenderer::setHotbarCountTextScale(float scale)
{
    m_hotbar.setCountTextScale(scale);
}

float UIRenderer::getHotbarCountTextScale() const
{
    return m_hotbar.getCountTextScale();
}

void UIRenderer::setInventoryCountTextOffsetX(float offsetX)
{
    m_inventoryPanel.itemGrid().setCountTextOffsetX(offsetX);
}

float UIRenderer::getInventoryCountTextOffsetX() const
{
    return m_inventoryPanel.itemGrid().getRenderParams().countTextOffsetX;
}

void UIRenderer::setInventoryCountTextOffsetY(float offsetY)
{
    m_inventoryPanel.itemGrid().setCountTextOffsetY(offsetY);
}

float UIRenderer::getInventoryCountTextOffsetY() const
{
    return m_inventoryPanel.itemGrid().getRenderParams().countTextOffsetY;
}

void UIRenderer::setInventoryCountTextScale(float scale)
{
    m_inventoryPanel.itemGrid().setCountTextScale(scale);
}

float UIRenderer::getInventoryCountTextScale() const
{
    return m_inventoryPanel.itemGrid().getRenderParams().countTextScale;
}

void UIRenderer::setHeldItemPreviewLayout(const HeldItemPreviewLayout& layout)
{
    m_heldItemPreview.setLayout(layout);
}

const HeldItemPreviewLayout& UIRenderer::getHeldItemPreviewLayout() const
{
    return m_heldItemPreview.getLayout();
}

void UIRenderer::triggerHeldItemPreviewActionAnimation()
{
    m_heldItemPreview.triggerActionAnimation();
}

void UIRenderer::setHeldItemPreviewActionAnimationActive(const bool active)
{
    m_heldItemPreview.setActionAnimationActive(active);
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
    m_commandInput.visible =(true);
    m_commandInputRequested = true;
}

void UIRenderer::renderPickable(const Pickable::SlotInfo* slots, int count,
                                float mouseX, float mouseY)
{
    if (!slots || count <= 0 || !m_resourceMgr) {
        return;
    }

    const bool wasVisible = m_inventoryPanel.visible;
    m_inventoryPanel.setVisible(true);
    m_inventoryPanel.setSlots(slots, count);
    m_inventoryPanel.render(makeContextFromViewport());
    m_inventoryPanel.setVisible(wasVisible);
}

UIEventResult UIRenderer::routeUIInput(const UIInputEvent& event) const
{
    UIEventResult aggregate = UIEventResult::Ignored;

    // Active scene has priority (menu screens overlay gameplay controls)
    if (m_activeScene && m_activeScene->isVisible()) {
        if (m_lastSceneContext.screenWidth <= 0 || m_lastSceneContext.screenHeight <= 0) {
            GLint viewport[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, viewport);
            m_lastSceneContext.screenWidth = std::max(1, viewport[2]);
            m_lastSceneContext.screenHeight = std::max(1, viewport[3]);
        }
        m_lastSceneContext.resourceMgr = m_resourceMgr;
        m_lastSceneContext.textRenderer = &m_text;
        m_lastSceneContext.pointerX = event.x;
        m_lastSceneContext.pointerY = event.y;
        m_activeScene->setInputContext(m_lastSceneContext);

        UIEventResult sceneResult = m_activeScene->onInput(event);
        if (sceneResult == UIEventResult::Consumed) return UIEventResult::Consumed;
        if (sceneResult == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    // Dispatch to UIWidget-based controls
    for (UIWidget* widget : m_widgetControls) {
        if (!widget || !widget->visible) {
            continue;
        }
        const UIEventResult widgetResult = widget->onInput(event, m_lastSceneContext);
        if (widgetResult == UIEventResult::Consumed) {
            return UIEventResult::Consumed;
        }
        if (widgetResult == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    return aggregate;
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

void UIRenderer::render(const Window& window,
                        const Inventory& inventory,
                        const PlayerStatsData& playerStats,
                        const HeldItemPreviewMotion& heldItemMotion,
                        const InputSnapshot& inputSnapshot)
{
    m_hotbar.setInventorySource(&inventory);
    m_inventoryPanel.setInventorySource(&inventory);
    m_commandInput.visible =(m_commandInputRequested);
    const UIRenderContext context = makeContextFromWindow(window, inventory, playerStats, heldItemMotion, inputSnapshot);
    m_lastSceneContext = context;
    m_crosshair.render(context);
    renderControls(context);
    m_commandInputRequested = false;
}

UIRenderContext UIRenderer::makeContextFromWindow(const Window& window,
                                                  const Inventory& inventory,
                                                  const PlayerStatsData& playerStats,
                                                  const HeldItemPreviewMotion& heldItemMotion,
                                                  const InputSnapshot& inputSnapshot) const
{
    UIRenderContext context;
    context.screenWidth = window.getWidth();
    context.screenHeight = window.getHeight();
    context.timeSeconds = static_cast<float>(Time::getGameTime());
    context.resourceMgr = m_resourceMgr;
    context.inventory = &inventory;
    context.playerStats = &playerStats;
    context.textRenderer = &m_text;
    context.commandInputText = &m_commandInput.getText();
    context.commandInputVisible = m_commandInput.visible;
    context.pointerX = inputSnapshot.mousePosition.x;
    context.pointerY = inputSnapshot.mousePosition.y;
    context.hasDraggedItem = inputSnapshot.draggedItem.active;
    context.draggedItemId = inputSnapshot.draggedItem.itemId;
    context.heldItemPreviewMotion = heldItemMotion;
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
    context.commandInputVisible = m_commandInput.visible;
    return context;
}

void UIRenderer::setActiveScene(UIScene* scene)
{
    m_activeScene = scene;
    if (m_activeScene) {
        m_activeScene->setInputContext(m_lastSceneContext);
    }
}

UIScene* UIRenderer::getActiveScene() const
{
    return m_activeScene;
}

ResourceMgr* UIRenderer::getResourceMgr() const
{
    return m_resourceMgr;
}

void UIRenderer::renderSceneOnly(const Window& window, const InputSnapshot& inputSnapshot)
{
    UIRenderContext context;
    context.screenWidth = window.getWidth();
    context.screenHeight = window.getHeight();
    context.timeSeconds = static_cast<float>(Time::getRawTime());
    context.resourceMgr = m_resourceMgr;
    context.textRenderer = &m_text;
    context.pointerX = inputSnapshot.mousePosition.x;
    context.pointerY = inputSnapshot.mousePosition.y;
    m_lastSceneContext = context;

    if (m_activeScene && m_activeScene->isVisible()) {
        m_activeScene->setInputContext(context);
        m_activeScene->render(context);
    }
}

void UIRenderer::renderControls(const UIRenderContext& context) const
{
    for (const UIWidget* widget : m_widgetControls) {
        if (!widget || !widget->visible) {
            continue;
        }
        widget->render(context);
    }

    // Render active scene on top of gameplay controls
    if (m_activeScene && m_activeScene->isVisible()) {
        m_activeScene->setInputContext(context);
        m_activeScene->render(context);
    }
}
