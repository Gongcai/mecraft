#include "UIRenderer.h"

#include <algorithm>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "engine/platform/Time.h"
#include "engine/input/InputManager.h"
#include "engine/platform/Window.h"
#include "../../player/Inventory.h"
#include "../../resource/ResourceMgr.h"
#include "../../renderer/core/Shader.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiResources.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"
#include "UIRenderUtils.h"
#include "UIScene.h"
#include "UIThemePresets.h"
#include "UIScaleConfig.h"

namespace {
constexpr int kBackdropBlurDownsample = 4;

}

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer()
{
    shutdown();
}

void UIRenderer::init(ResourceMgr& resourceMgr)
{
    m_resourceMgr = &resourceMgr;
    m_theme = UIThemePresets::dark();

    // Crosshair - no scaling (always pixel-perfect)
    m_crosshair.init(resourceMgr);
    m_crosshair.setScaleStrategy(UIScaleStrategy::None);

    m_text.init(resourceMgr);

    // Hotbar - uniform scaling with GUI scale
    m_hotbar.init(resourceMgr);
    m_hotbar.visible = true;
    m_hotbar.setScaleStrategy(UIScaleStrategy::Uniform);

    // HUD (health/food bars) - uniform scaling
    m_hud.init(resourceMgr);
    m_hud.visible = true;
    m_hud.setScaleStrategy(UIScaleStrategy::Uniform);

    // Death screen
    m_deathBackdrop.init(resourceMgr);
    m_deathBackdrop.visible = true;
    m_deathBackdrop.setUseLocalColors(true);
    m_deathBackdrop.setBackgroundColor({0.34f, 0.02f, 0.02f, 0.64f});
    m_deathBackdrop.setBorderWidth(0.0f);
    m_deathTitle.init(resourceMgr);
    m_deathTitle.setText("You died");
    m_deathTitle.setTextScale(4.0f);
    m_deathTitle.setTextColor({1.0f, 0.18f, 0.16f, 1.0f});
    m_deathTitle.setShadowEnabled(true);
    m_deathTitle.setShadowOffset(2.0f, -2.0f);
    m_deathPrompt.init(resourceMgr);
    m_deathPrompt.setText("Press R to respawn");
    m_deathPrompt.setTextScale(1.6f);
    m_deathPrompt.setTextColor({1.0f, 1.0f, 1.0f, 0.92f});
    m_deathPrompt.setShadowEnabled(true);

    // Inventory panels - uniform scaling
    m_inventoryPanel.init(resourceMgr);
    m_inventoryPanel.visible = false;
    m_inventoryPanel.setScaleStrategy(UIScaleStrategy::Uniform);

    m_storagePanel.init(resourceMgr);
    m_storagePanel.visible = false;
    m_storagePanel.setScaleStrategy(UIScaleStrategy::Uniform);

    m_machinePanel.init(resourceMgr);
    m_machinePanel.visible = false;
    m_machinePanel.setScaleStrategy(UIScaleStrategy::Uniform);

    m_creativeInventoryPanel.init(resourceMgr);
    m_creativeInventoryPanel.visible = false;
    m_creativeInventoryPanel.setScaleStrategy(UIScaleStrategy::Uniform);

    // Command input and console - text adaptive
    m_commandInput.init(resourceMgr);
    m_commandInput.visible = false;
    m_commandInput.setScaleStrategy(UIScaleStrategy::TextOnly);

    m_console.init(resourceMgr);
    m_console.visible = true;
    m_console.setTextRenderer(&m_text);
    m_console.setMaxLines(m_consoleMaxLines);
    m_console.setScaleStrategy(UIScaleStrategy::TextOnly);

    m_widgetControls = {
        &m_hud,
        &m_console,
        &m_commandInput,
        &m_hotbar,
        &m_inventoryPanel,
        &m_storagePanel,
        &m_machinePanel,
        &m_creativeInventoryPanel,
    };

    m_lastSceneContext = {};
    m_lastSceneContext.resourceMgr = m_resourceMgr;
    m_lastSceneContext.humanoidRenderer = m_humanoidRenderer;
    m_lastSceneContext.textRenderer = &m_text;
}

void UIRenderer::shutdown()
{
    m_crosshair.shutdown();

    m_console.shutdown();
    m_commandInput.shutdown();
    m_text.shutdown();
    m_creativeInventoryPanel.shutdown();
    m_machinePanel.shutdown();
    m_storagePanel.shutdown();
    m_inventoryPanel.shutdown();
    m_deathPrompt.shutdown();
    m_deathTitle.shutdown();
    m_deathBackdrop.shutdown();
    m_hud.shutdown();
    m_hotbar.shutdown();
    m_commandInputRequested = false;
    m_lastSceneContext = {};
    destroyBackdropBlurTargets();
    m_resourceMgr = nullptr;
    m_humanoidRenderer = nullptr;
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

void UIRenderer::setGUIScale(GUIScale scale)
{
    m_guiScale = scale;
}

GUIScale UIRenderer::getGUIScale() const
{
    return m_guiScale;
}

void UIRenderer::setCommandCaretBlinkPeriodMs(float periodMs)
{
    m_commandInput.setCaretBlinkPeriodMs(periodMs);
}

float UIRenderer::getCommandCaretBlinkPeriodMs() const
{
    return m_commandInput.getCaretBlinkPeriodMs();
}

void UIRenderer::setTheme(const UITheme& theme)
{
    m_theme = theme;
}

void UIRenderer::setLocaleManager(const LocaleManager* localeManager)
{
    m_localeManager = localeManager;
}

const UITheme& UIRenderer::getTheme() const
{
    return m_theme;
}

UITheme& UIRenderer::getTheme()
{
    return m_theme;
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
    m_inventoryPanel.render(makeContextFromSurface());
    m_inventoryPanel.setVisible(wasVisible);
}

UIEventResult UIRenderer::routeUIInput(const UIInputEvent& event) const
{
    UIEventResult aggregate = UIEventResult::Ignored;
    UIInputEvent refEvent = event;

    // Active scene has priority (menu screens overlay gameplay controls)
    if (m_activeScene && m_activeScene->visible) {
        if (m_lastSceneContext.screenWidth <= 0 || m_lastSceneContext.screenHeight <= 0) {
            const float vpW = static_cast<float>(m_surfaceWidth);
            const float vpH = static_cast<float>(m_surfaceHeight);
            m_lastSceneContext.scaleConfig = UIScaleConfig::create(vpW, vpH, m_guiScale);
            m_lastSceneContext.screenWidth = m_lastSceneContext.scaleConfig.virtualWidth;
            m_lastSceneContext.screenHeight = m_lastSceneContext.scaleConfig.virtualHeight;
        }
    }

    const float pixelScale = m_lastSceneContext.pixelScale();
    refEvent.x /= pixelScale;
    refEvent.y /= pixelScale;
    m_lastSceneContext.resourceMgr = m_resourceMgr;
    m_lastSceneContext.textRenderer = &m_text;
    m_lastSceneContext.pointerX = refEvent.x;
    m_lastSceneContext.pointerY = refEvent.y;

    if (m_activeScene && m_activeScene->visible) {
        m_activeScene->setInputContext(m_lastSceneContext);

        UIEventResult sceneResult = m_activeScene->onInput(refEvent, m_lastSceneContext);
        if (sceneResult == UIEventResult::Consumed) return UIEventResult::Consumed;
        if (sceneResult == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    // Floating widget overlays are rendered above normal controls, so route their input first.
    for (auto it = m_widgetControls.rbegin(); it != m_widgetControls.rend(); ++it) {
        UIWidget* widget = *it;
        if (!widget || !widget->visible) {
            continue;
        }
        const UIEventResult overlayResult = widget->onOverlayInput(refEvent, m_lastSceneContext);
        if (overlayResult == UIEventResult::Consumed) {
            return UIEventResult::Consumed;
        }
        if (overlayResult == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    // Dispatch to UIWidget-based controls
    for (UIWidget* widget : m_widgetControls) {
        if (!widget || !widget->visible) {
            continue;
        }
        const UIEventResult widgetResult = widget->onInput(refEvent, m_lastSceneContext);
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
    if (visible) {
        m_storagePanel.setVisible(false);
        m_machinePanel.setVisible(false);
        m_creativeInventoryPanel.setVisible(false);
    }
}

void UIRenderer::setHumanoidRenderer(HumanoidRenderer* humanoidRenderer)
{
    m_humanoidRenderer = humanoidRenderer;
    m_lastSceneContext.humanoidRenderer = humanoidRenderer;
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

void UIRenderer::setStoragePanelVisible(const bool visible)
{
    m_storagePanel.setVisible(visible);
    if (visible) {
        m_inventoryPanel.setVisible(false);
        m_machinePanel.setVisible(false);
        m_creativeInventoryPanel.setVisible(false);
    }
}

void UIRenderer::setStoragePanelDefinition(const ui::ContainerUiDef& definition)
{
    m_storagePanel.setDefinition(definition);
}

void UIRenderer::setStoragePanelSource(const BlockEntityInventory* storageInventory)
{
    m_storagePanel.setStorageSource(storageInventory);
}

int UIRenderer::getStoragePanelLastActivatedSlot() const
{
    return m_storagePanel.getContainerLastActivatedSlot();
}

int UIRenderer::getStoragePanelPlayerLastActivatedSlot() const
{
    return m_storagePanel.getPlayerLastActivatedSlot();
}

int UIRenderer::getStoragePanelHoveredSlot() const
{
    return m_storagePanel.getContainerHoveredSlot();
}

int UIRenderer::getStoragePanelPlayerHoveredSlot() const
{
    return m_storagePanel.getPlayerHoveredSlot();
}

void UIRenderer::clearStoragePanelActivations()
{
    m_storagePanel.clearActivations();
}

void UIRenderer::setMachinePanelVisible(const bool visible)
{
    m_machinePanel.setVisible(visible);
    if (visible) {
        m_inventoryPanel.setVisible(false);
        m_storagePanel.setVisible(false);
        m_creativeInventoryPanel.setVisible(false);
    }
}

void UIRenderer::setMachinePanelDefinition(const ui::ContainerUiDef& definition)
{
    m_machinePanel.setDefinition(definition);
}

void UIRenderer::setMachinePanelSource(const MachineInventory* machine)
{
    m_machinePanel.setMachineSource(machine);
}

void UIRenderer::setMachinePanelProgress(const float burnFraction, const float cookFraction)
{
    m_machinePanel.setProgress(burnFraction, cookFraction);
}

int UIRenderer::getMachinePanelLastActivatedSlot() const
{
    return m_machinePanel.getContainerLastActivatedSlot();
}

int UIRenderer::getMachinePanelPlayerLastActivatedSlot() const
{
    return m_machinePanel.getPlayerLastActivatedSlot();
}

int UIRenderer::getMachinePanelHoveredSlot() const
{
    return m_machinePanel.getContainerHoveredSlot();
}

int UIRenderer::getMachinePanelPlayerHoveredSlot() const
{
    return m_machinePanel.getPlayerHoveredSlot();
}

void UIRenderer::clearMachinePanelActivations()
{
    m_machinePanel.clearActivations();
}

void UIRenderer::setCreativeInventoryVisible(const bool visible)
{
    m_creativeInventoryPanel.setVisible(visible);
    if (visible) {
        m_inventoryPanel.setVisible(false);
        m_storagePanel.setVisible(false);
        m_machinePanel.setVisible(false);
    }
}

void UIRenderer::setCreativeInventoryTab(const CreativeInventoryTab tab)
{
    m_creativeInventoryPanel.setTab(tab);
}

CreativeInventoryTab UIRenderer::getCreativeInventoryTab() const
{
    return m_creativeInventoryPanel.getTab();
}

int UIRenderer::getCreativeInventoryLastActivatedSlot() const
{
    return m_creativeInventoryPanel.getLastActivatedSlot();
}

ItemID UIRenderer::getCreativeInventoryLastActivatedCreativeItem() const
{
    return m_creativeInventoryPanel.getLastActivatedCreativeItem();
}

int UIRenderer::getCreativeInventoryHoveredInventorySlot() const
{
    return m_creativeInventoryPanel.getHoveredInventorySlot();
}

void UIRenderer::clearCreativeInventoryActivations()
{
    m_creativeInventoryPanel.clearActivations();
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
                        RhiDevice& rhiDevice,
                        const Inventory& inventory,
                        const PlayerStatsData& playerStats,
                        const InputSnapshot& inputSnapshot)
{
    UIRenderContext context = prepareRenderContext(window, rhiDevice, inventory, playerStats, inputSnapshot);
    renderPrepared(context);
}

UIRenderContext UIRenderer::prepareRenderContext(const Window& window,
                                                 RhiDevice& rhiDevice,
                                                 const Inventory& inventory,
                                                 const PlayerStatsData& playerStats,
                                                 const InputSnapshot& inputSnapshot)
{
    m_surfaceWidth = std::max(1, window.getWidth());
    m_surfaceHeight = std::max(1, window.getHeight());

    m_hotbar.setInventorySource(&inventory);
    m_inventoryPanel.setInventorySource(&inventory);
    m_storagePanel.setPlayerInventorySource(&inventory);
    m_machinePanel.setPlayerInventorySource(&inventory);
    m_creativeInventoryPanel.setInventorySource(&inventory);
    m_commandInput.visible =(m_commandInputRequested);

    UIRenderContext context = makeContextFromWindow(window, inventory, playerStats, inputSnapshot);
    if (m_activeScene && m_activeScene->visible) {
        prepareBackdropBlur(context, rhiDevice);
    }
    m_lastSceneContext = context;

    return context;
}

void UIRenderer::renderPrepared(const UIRenderContext& context)
{
    m_crosshair.render(context);
    renderControls(context);
    m_commandInputRequested = false;
}

UIRenderContext UIRenderer::makeContextFromWindow(const Window& window,
                                                  const Inventory& inventory,
                                                  const PlayerStatsData& playerStats,
                                                  const InputSnapshot& inputSnapshot) const
{
    UIRenderContext context;
    const float actualW = static_cast<float>(std::max(1, window.getWidth()));
    const float actualH = static_cast<float>(std::max(1, window.getHeight()));

    context.scaleConfig = UIScaleConfig::create(actualW, actualH, m_guiScale);
    context.screenWidth = context.scaleConfig.virtualWidth;
    context.screenHeight = context.scaleConfig.virtualHeight;

    context.timeSeconds = static_cast<float>(Time::getRawTime());
    context.resourceMgr = m_resourceMgr;
    context.humanoidRenderer = m_humanoidRenderer;
    context.inventory = &inventory;
    context.playerStats = &playerStats;
    context.playerDead = playerStats.isDead;
    context.textRenderer = &m_text;
    context.commandInputText = &m_commandInput.getText();
    context.commandInputVisible = m_commandInput.visible;
    context.pointerX = inputSnapshot.mousePosition.x / context.pixelScale();
    context.pointerY = inputSnapshot.mousePosition.y / context.pixelScale();
    context.hasDraggedItem = inputSnapshot.draggedItem.active;
    context.draggedItemId = inputSnapshot.draggedItem.itemId;
    context.theme = &m_theme;
    context.localeManager = m_localeManager;
    return context;
}

UIRenderContext UIRenderer::makeContextFromSurface() const
{
    UIRenderContext context;
    const float actualW = static_cast<float>(m_surfaceWidth);
    const float actualH = static_cast<float>(m_surfaceHeight);

    context.scaleConfig = UIScaleConfig::create(actualW, actualH, m_guiScale);
    context.screenWidth = context.scaleConfig.virtualWidth;
    context.screenHeight = context.scaleConfig.virtualHeight;
    context.timeSeconds = static_cast<float>(Time::getRawTime());
    context.resourceMgr = m_resourceMgr;
    context.humanoidRenderer = m_humanoidRenderer;
    context.textRenderer = &m_text;
    context.commandInputText = &m_commandInput.getText();
    context.commandInputVisible = m_commandInput.visible;
    context.theme = &m_theme;
    context.localeManager = m_localeManager;
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

void UIRenderer::renderSceneOnly(const Window& window,
                                 RhiDevice& rhiDevice,
                                 const InputSnapshot& inputSnapshot)
{
    UIRenderContext context = prepareSceneContext(window, rhiDevice, inputSnapshot);
    renderSceneOnlyPrepared(context);
}

UIRenderContext UIRenderer::prepareSceneContext(const Window& window,
                                                RhiDevice& rhiDevice,
                                                const InputSnapshot& inputSnapshot)
{
    const int windowW = std::max(1, window.getWidth());
    const int windowH = std::max(1, window.getHeight());
    m_surfaceWidth = windowW;
    m_surfaceHeight = windowH;

    UIRenderContext context;
    const float actualW = static_cast<float>(windowW);
    const float actualH = static_cast<float>(windowH);

    context.scaleConfig = UIScaleConfig::create(actualW, actualH, m_guiScale);
    context.screenWidth = context.scaleConfig.virtualWidth;
    context.screenHeight = context.scaleConfig.virtualHeight;
    context.timeSeconds = static_cast<float>(Time::getRawTime());
    context.resourceMgr = m_resourceMgr;
    context.humanoidRenderer = m_humanoidRenderer;
    context.textRenderer = &m_text;
    context.commandInputText = &m_commandInput.getText();
    context.commandInputVisible = m_commandInput.visible;
    context.theme = &m_theme;
    context.localeManager = m_localeManager;
    context.pointerX = inputSnapshot.mousePosition.x / context.pixelScale();
    context.pointerY = inputSnapshot.mousePosition.y / context.pixelScale();
    m_lastSceneContext = context;

    if (m_activeScene && m_activeScene->visible) {
        prepareBackdropBlur(context, rhiDevice);
    }
    return context;
}

void UIRenderer::renderSceneOnlyPrepared(const UIRenderContext& context)
{
    if (m_activeScene && m_activeScene->visible) {
        m_activeScene->setInputContext(context);
        m_activeScene->render(context);
    }
}

void UIRenderer::renderControls(const UIRenderContext& context)
{
    UIRenderContext renderContext = context;

    const UIRenderUtils::UIScopeGuard uiScope;

    for (const UIWidget* widget : m_widgetControls) {
        if (!widget || !widget->visible) {
            continue;
        }
        widget->render(renderContext);
    }

    if (renderContext.playerDead) {
        renderDeathOverlay(renderContext);
    }

    // Render active scene on top of gameplay controls
    if (m_activeScene && m_activeScene->visible) {
        m_activeScene->setInputContext(renderContext);
        m_activeScene->render(renderContext);
    }
}

void UIRenderer::renderDeathOverlay(const UIRenderContext& context)
{
    m_deathBackdrop.anchor = Anchor::BottomLeft;
    m_deathBackdrop.anchorOffsetX = 0.0f;
    m_deathBackdrop.anchorOffsetY = 0.0f;
    m_deathBackdrop.width = static_cast<float>(context.screenWidth);
    m_deathBackdrop.height = static_cast<float>(context.screenHeight);
    m_deathBackdrop.render(context);

    const float screenW = static_cast<float>(context.screenWidth);
    const float screenH = static_cast<float>(context.screenHeight);

    const float titleW = context.textRenderer ? m_deathTitle.measureTextWidth(*context.textRenderer) : 0.0f;
    const float titleH = context.textRenderer ? m_deathTitle.measureTextHeight(*context.textRenderer) : 0.0f;
    m_deathTitle.anchor = Anchor::BottomLeft;
    m_deathTitle.anchorOffsetX = (screenW - titleW) * 0.5f;
    m_deathTitle.anchorOffsetY = screenH * 0.5f + titleH * 0.4f;
    m_deathTitle.width = titleW;
    m_deathTitle.height = titleH;
    m_deathTitle.render(context);

    const float promptW = context.textRenderer ? m_deathPrompt.measureTextWidth(*context.textRenderer) : 0.0f;
    const float promptH = context.textRenderer ? m_deathPrompt.measureTextHeight(*context.textRenderer) : 0.0f;
    m_deathPrompt.anchor = Anchor::BottomLeft;
    m_deathPrompt.anchorOffsetX = (screenW - promptW) * 0.5f;
    m_deathPrompt.anchorOffsetY = screenH * 0.5f - promptH * 1.6f;
    m_deathPrompt.width = promptW;
    m_deathPrompt.height = promptH;
    m_deathPrompt.render(context);
}

bool UIRenderer::ensureBackdropBlurTargets(const int sourceWidth,
                                           const int sourceHeight,
                                           RhiDevice& rhiDevice) const
{
    const int blurWidth = std::max(1, sourceWidth / kBackdropBlurDownsample);
    const int blurHeight = std::max(1, sourceHeight / kBackdropBlurDownsample);
    const bool sizeChanged = sourceWidth != m_backdropSourceWidth ||
                             sourceHeight != m_backdropSourceHeight ||
                             blurWidth != m_backdropBlurWidth ||
                             blurHeight != m_backdropBlurHeight;

    if (m_backdropRhiViewDevice != nullptr && m_backdropRhiViewDevice != &rhiDevice) {
        destroyBackdropBlurTargets();
    }
    if (sizeChanged) {
        destroyBackdropBlurTargets();
    }
    m_backdropRhiViewDevice = &rhiDevice;

    if (!m_backdropSource.isValid()) {
        RhiTextureDesc desc;
        desc.debugName = "UiBackdrop.Source";
        desc.format = RhiTextureFormat::Rgba8Unorm;
        desc.width = static_cast<uint32_t>(sourceWidth);
        desc.height = static_cast<uint32_t>(sourceHeight);
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
        m_backdropSource = rhiDevice.createTexture(desc, nullptr);
    }
    if (!m_backdropSourceView.isValid() && m_backdropSource.isValid()) {
        RhiTextureViewDesc desc;
        desc.texture = m_backdropSource;
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba8Unorm;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;
        m_backdropSourceView = rhiDevice.createTextureView(desc);
    }

    for (int i = 0; i < 2; ++i) {
        if (!m_backdropBlur[i].isValid()) {
            RhiTextureDesc desc;
            desc.debugName = i == 0 ? "UiBackdrop.BlurHorizontal" : "UiBackdrop.BlurVertical";
            desc.format = RhiTextureFormat::Rgba8Unorm;
            desc.width = static_cast<uint32_t>(blurWidth);
            desc.height = static_cast<uint32_t>(blurHeight);
            desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment);
            m_backdropBlur[i] = rhiDevice.createTexture(desc, nullptr);
        }

        if (!m_backdropBlurView[i].isValid() && m_backdropBlur[i].isValid()) {
            RhiTextureViewDesc desc;
            desc.texture = m_backdropBlur[i];
            desc.viewType = RhiTextureViewType::Texture2D;
            desc.format = RhiTextureFormat::Rgba8Unorm;
            desc.baseMip = 0;
            desc.mipCount = 1;
            desc.baseLayer = 0;
            desc.layerCount = 1;
            m_backdropBlurView[i] = rhiDevice.createTextureView(desc);
        }
    }

    if (!m_backdropSource.isValid() ||
        !m_backdropBlur[0].isValid() ||
        !m_backdropBlur[1].isValid()) {
        destroyBackdropBlurTargets();
        return false;
    }

    if (!m_backdropBlurView[0].isValid() || !m_backdropBlurView[1].isValid()) {
        destroyBackdropBlurTargets();
        return false;
    }
    if (!m_backdropSourceView.isValid()) {
        destroyBackdropBlurTargets();
        return false;
    }

    m_backdropRhiViewDevice = &rhiDevice;
    m_backdropSourceWidth = sourceWidth;
    m_backdropSourceHeight = sourceHeight;
    m_backdropBlurWidth = blurWidth;
    m_backdropBlurHeight = blurHeight;
    return true;
}

bool UIRenderer::ensureBackdropBlurPipeline(RhiDevice& rhiDevice) const
{
    if (m_backdropRhiViewDevice != nullptr && m_backdropRhiViewDevice != &rhiDevice) {
        destroyBackdropBlurTargets();
    }
    if (m_backdropBlurPipeline.isValid()) {
        return true;
    }

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/blur_rhi.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "UiBackdropBlur.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_backdropBlurVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "UiBackdropBlur.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_backdropBlurFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_backdropBlurVertexShader.isValid() || !m_backdropBlurFragmentShader.isValid()) {
        destroyBackdropBlurPipeline();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_backdropBlurSampler = rhiDevice.createSampler(samplerDesc);
    if (!m_backdropBlurSampler.isValid()) {
        destroyBackdropBlurPipeline();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "UiBackdropBlur.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_backdropBlurBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_backdropBlurBindGroupLayout.isValid()) {
        destroyBackdropBlurPipeline();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "UiBackdropBlur.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_backdropBlurBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_backdropBlurPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_backdropBlurPipelineLayout.isValid()) {
        destroyBackdropBlurPipeline();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiBackdropBlur.Pipeline";
    pipelineDesc.vertexShader = m_backdropBlurVertexShader;
    pipelineDesc.fragmentShader = m_backdropBlurFragmentShader;
    pipelineDesc.layout = m_backdropBlurPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_backdropBlurPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_backdropBlurPipeline.isValid()) {
        destroyBackdropBlurPipeline();
        return false;
    }

    m_backdropRhiViewDevice = &rhiDevice;
    return true;
}

bool UIRenderer::ensureBackdropBlurBindGroups(RhiDevice& rhiDevice) const
{
    if (!ensureBackdropBlurPipeline(rhiDevice)) {
        return false;
    }

    const RhiTextureViewHandle inputViews[3] = {
        m_backdropSourceView,
        m_backdropBlurView[0],
        m_backdropBlurView[1]
    };
    for (int i = 0; i < 3; ++i) {
        if (m_backdropBlurBindGroup[i].isValid()) {
            continue;
        }
        if (!inputViews[i].isValid()) {
            destroyBackdropBlurBindGroups();
            return false;
        }

        RhiBindGroupDesc bindGroupDesc;
        bindGroupDesc.layout = m_backdropBlurBindGroupLayout;
        RhiBindGroupEntry textureEntry;
        textureEntry.binding = 0u;
        textureEntry.resource.combinedTextureSampler.textureView = inputViews[i];
        textureEntry.resource.combinedTextureSampler.sampler = m_backdropBlurSampler;
        bindGroupDesc.entries.push_back(textureEntry);
        m_backdropBlurBindGroup[i] = rhiDevice.createBindGroup(bindGroupDesc);
        if (!m_backdropBlurBindGroup[i].isValid()) {
            destroyBackdropBlurBindGroups();
            return false;
        }
    }
    return true;
}

void UIRenderer::prepareBackdropBlur(UIRenderContext& context, RhiDevice& rhiDevice) const
{
    context.backdropBlur = {};
    context.backdropBlurPrepared = true;
    context.backdropSourceWidth = 0;
    context.backdropSourceHeight = 0;
    context.backdropBlurWidth = 0;
    context.backdropBlurHeight = 0;

    const int sourceWidth = std::max(1, context.screenWidth);
    const int sourceHeight = std::max(1, context.screenHeight);

    if (!ensureBackdropBlurTargets(sourceWidth, sourceHeight, rhiDevice)) {
        return;
    }
    if (!ensureBackdropBlurBindGroups(rhiDevice)) {
        return;
    }

    if (!m_backdropSource.isValid() || !m_backdropBlur[0].isValid() ||
        !m_backdropBlur[1].isValid() || !m_backdropBlurView[0].isValid() ||
        !m_backdropBlurView[1].isValid()) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();

    RhiTextureBlit sourceBlit;
    sourceBlit.srcView = rhiDevice.currentSwapchainColorView();
    sourceBlit.dstView = m_backdropSourceView;
    commandList.blitTexture(sourceBlit);

    auto blurPass = [&](const uint32_t bindGroupIndex,
                        const RhiTextureViewHandle outputView,
                        const glm::vec2 direction) {
        RhiColorAttachment colorAttachment;
        colorAttachment.view = outputView;
        colorAttachment.loadOp = RhiLoadOp::DontCare;
        colorAttachment.storeOp = RhiStoreOp::Store;

        RhiRenderingInfo renderingInfo;
        renderingInfo.debugName = "UiBackdropBlur";
        renderingInfo.renderArea = {
            0,
            0,
            static_cast<uint32_t>(std::max(1, m_backdropBlurWidth)),
            static_cast<uint32_t>(std::max(1, m_backdropBlurHeight))
        };
        renderingInfo.colorAttachments = &colorAttachment;
        renderingInfo.colorAttachmentCount = 1u;

        commandList.beginRendering(renderingInfo);
        commandList.setGraphicsPipeline(m_backdropBlurPipeline);
        commandList.setBindGroup(0u, m_backdropBlurBindGroup[bindGroupIndex]);
        const glm::vec4 pushConstants(direction.x, direction.y, 0.0f, 0.0f);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(3u, 1u, 0u, 0u);
        commandList.endRendering();
    };

    blurPass(0u, m_backdropBlurView[0], glm::vec2(1.0f / static_cast<float>(sourceWidth), 0.0f));
    blurPass(1u, m_backdropBlurView[1], glm::vec2(0.0f, 1.0f / static_cast<float>(m_backdropBlurHeight)));
    blurPass(2u, m_backdropBlurView[0], glm::vec2(1.0f / static_cast<float>(m_backdropBlurWidth), 0.0f));
    blurPass(1u, m_backdropBlurView[1], glm::vec2(0.0f, 1.0f / static_cast<float>(m_backdropBlurHeight)));

    rhiDevice.submitFrame(commandList);

    context.backdropBlur = m_backdropBlur[1];
    context.backdropSourceWidth = sourceWidth;
    context.backdropSourceHeight = sourceHeight;
    context.backdropBlurWidth = m_backdropBlurWidth;
    context.backdropBlurHeight = m_backdropBlurHeight;
}

void UIRenderer::destroyBackdropBlurTargets() const
{
    RhiDevice* device = m_backdropRhiViewDevice;
    destroyBackdropBlurPipeline();
    destroyBackdropBlurViews();
    if (device != nullptr) {
        if (m_backdropSource.isValid()) device->destroyTexture(m_backdropSource);
        if (m_backdropBlur[0].isValid()) device->destroyTexture(m_backdropBlur[0]);
        if (m_backdropBlur[1].isValid()) device->destroyTexture(m_backdropBlur[1]);
    }
    m_backdropSource = {};
    m_backdropBlur[0] = {};
    m_backdropBlur[1] = {};
    m_backdropSourceWidth = 0;
    m_backdropSourceHeight = 0;
    m_backdropBlurWidth = 0;
    m_backdropBlurHeight = 0;
}

void UIRenderer::destroyBackdropBlurBindGroups() const
{
    if (m_backdropRhiViewDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_backdropBlurBindGroup) {
            if (bindGroup.isValid()) {
                m_backdropRhiViewDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    } else {
        m_backdropBlurBindGroup[0] = {};
        m_backdropBlurBindGroup[1] = {};
        m_backdropBlurBindGroup[2] = {};
    }
}

void UIRenderer::destroyBackdropBlurPipeline() const
{
    destroyBackdropBlurBindGroups();
    if (m_backdropRhiViewDevice != nullptr) {
        if (m_backdropBlurPipeline.isValid()) {
            m_backdropRhiViewDevice->destroyPipeline(m_backdropBlurPipeline);
        }
        if (m_backdropBlurVertexShader.isValid()) {
            m_backdropRhiViewDevice->destroyShader(m_backdropBlurVertexShader);
        }
        if (m_backdropBlurFragmentShader.isValid()) {
            m_backdropRhiViewDevice->destroyShader(m_backdropBlurFragmentShader);
        }
        if (m_backdropBlurPipelineLayout.isValid()) {
            m_backdropRhiViewDevice->destroyPipelineLayout(m_backdropBlurPipelineLayout);
        }
        if (m_backdropBlurBindGroupLayout.isValid()) {
            m_backdropRhiViewDevice->destroyBindGroupLayout(m_backdropBlurBindGroupLayout);
        }
        if (m_backdropBlurSampler.isValid()) {
            m_backdropRhiViewDevice->destroySampler(m_backdropBlurSampler);
        }
    }
    m_backdropBlurPipeline = {};
    m_backdropBlurVertexShader = {};
    m_backdropBlurFragmentShader = {};
    m_backdropBlurPipelineLayout = {};
    m_backdropBlurBindGroupLayout = {};
    m_backdropBlurSampler = {};
}

void UIRenderer::destroyBackdropBlurViews() const
{
    destroyBackdropBlurBindGroups();
    if (m_backdropRhiViewDevice != nullptr) {
        if (m_backdropSourceView.isValid()) {
            m_backdropRhiViewDevice->destroyTextureView(m_backdropSourceView);
        }
        m_backdropSourceView = {};
        for (RhiTextureViewHandle& view : m_backdropBlurView) {
            if (view.isValid()) {
                m_backdropRhiViewDevice->destroyTextureView(view);
            }
            view = {};
        }
    } else {
        m_backdropSourceView = {};
        m_backdropBlurView[0] = {};
        m_backdropBlurView[1] = {};
    }
    m_backdropRhiViewDevice = nullptr;
}
