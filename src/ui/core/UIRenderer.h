#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <cstdint>

#include "../hud/CommandInputOverlay.h"
#include "../widgets/ConsoleDisplayBox.h"
#include "../hud/ConsoleOverlay.h"
#include "../inventory/CreativeInventoryPanelControl.h"
#include "../hud/CrosshairControl.h"
#include "../hud/HotbarControl.h"
#include "../hud/HudControl.h"
#include "../inventory/DataDrivenContainerPanelControl.h"
#include "../inventory/InventoryPanelControl.h"
#include "../hud/Pickable.h"
#include "../font/TextRenderer.h"
#include "../widgets/UIPanel.h"
#include "../widgets/UIText.h"
#include "UIRenderContext.h"
#include "UITheme.h"
#include "UIScaleConfig.h"

struct GameResources;
class RhiDevice;
class RhiCommandListPool;
class Inventory;
class LocaleManager;
class CraftingSystem;
class HumanoidRenderer;
struct InputSnapshot;
class UIScene;
class UIWidget;

class UIRenderer {
public:
    UIRenderer();
    ~UIRenderer();

    // GUI Scale management (new unified system)
    void setGUIScale(GUIScale scale);
    [[nodiscard]] GUIScale getGUIScale() const;

    void init(GameResources& resources, RhiDevice& rhiDevice, RhiCommandListPool& commandListPool);
    void shutdown();

    UIRenderContext prepareRenderContext(int surfaceWidth, int surfaceHeight, RhiDevice& rhiDevice,
                                         const Inventory& inventory, const PlayerStatsData& playerStats,
                                         const InputSnapshot& inputSnapshot);
    void renderPrepared(const UIRenderContext& context);
    void renderCommandInputBox(const std::string& text);
    [[nodiscard]] UIEventResult routeUIInput(const UIInputEvent& event) const;
    void setInventoryPanelVisible(bool visible);
    void setHumanoidRenderer(HumanoidRenderer* humanoidRenderer);
    void setInventoryPanelLayout(const InventoryPanelLayout& layout);
    [[nodiscard]] const InventoryPanelLayout& getInventoryPanelLayout() const;
    [[nodiscard]] int getInventoryPanelLastActivatedSlot() const;
    [[nodiscard]] int getInventoryPanelHoveredSlot() const;

    void setStoragePanelVisible(bool visible);
    void setStoragePanelDefinition(const ui::ContainerUiDef& definition);
    void setStoragePanelSource(const BlockEntityInventory* storageInventory);
    [[nodiscard]] int getStoragePanelLastActivatedSlot() const;
    [[nodiscard]] int getStoragePanelPlayerLastActivatedSlot() const;
    [[nodiscard]] int getStoragePanelHoveredSlot() const;
    [[nodiscard]] int getStoragePanelPlayerHoveredSlot() const;
    void clearStoragePanelActivations();

    void setMachinePanelVisible(bool visible);
    void setMachinePanelDefinition(const ui::ContainerUiDef& definition);
    void setMachinePanelSource(const MachineInventory* machine);
    void setMachinePanelProgress(float burnFraction, float cookFraction);
    [[nodiscard]] int getMachinePanelLastActivatedSlot() const;
    [[nodiscard]] int getMachinePanelPlayerLastActivatedSlot() const;
    [[nodiscard]] int getMachinePanelHoveredSlot() const;
    [[nodiscard]] int getMachinePanelPlayerHoveredSlot() const;
    void clearMachinePanelActivations();

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

    // Records text atlas and vertex uploads before the UI render pass begins.
    bool prepareTextFrame(RhiCommandList& commandList);

    // Scene management for menu screens
    void setActiveScene(UIScene* scene);
    [[nodiscard]] UIScene* getActiveScene() const;
    [[nodiscard]] GameResources* getResources() const;
    [[nodiscard]] RhiDevice* getRhiDevice() const;
    UIRenderContext prepareSceneContext(int surfaceWidth, int surfaceHeight, RhiDevice& rhiDevice,
                                        const InputSnapshot& inputSnapshot);
    void renderSceneOnlyPrepared(const UIRenderContext& context);

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

    [[nodiscard]] RhiBindGroupHandle resolveImageBindGroup(RhiTextureHandle texture) const;

    void setHotbarCountTextScale(float scale);
    [[nodiscard]] float getHotbarCountTextScale() const;

    void setInventoryCountTextOffsetX(float offsetX);
    [[nodiscard]] float getInventoryCountTextOffsetX() const;
    void setInventoryCountTextOffsetY(float offsetY);
    [[nodiscard]] float getInventoryCountTextOffsetY() const;
    void setInventoryCountTextScale(float scale);
    [[nodiscard]] float getInventoryCountTextScale() const;

private:
    [[nodiscard]] UIRenderContext makeContextFromSurface(int surfaceWidth, int surfaceHeight,
                                                         const Inventory& inventory, const PlayerStatsData& playerStats,
                                                         const InputSnapshot& inputSnapshot) const;
    void renderControls(const UIRenderContext& context);
    void renderDeathOverlay(const UIRenderContext& context);
    void prepareBackdropBlur(UIRenderContext& context, RhiDevice& rhiDevice) const;
    bool ensureBackdropBlurTargets(int sourceWidth, int sourceHeight, RhiDevice& rhiDevice) const;
    bool ensureBackdropBlurPipeline(RhiDevice& rhiDevice) const;
    bool ensureBackdropBlurBindGroups(RhiDevice& rhiDevice) const;
    void destroyBackdropBlurBindGroups() const;
    void destroyBackdropBlurPipeline() const;
    void destroyBackdropBlurViews() const;
    void destroyBackdropBlurTargets() const;
    void initPanelRhiResources(RhiDevice& rhiDevice);
    void destroyPanelRhiResources();
    bool ensurePanelGlassBindGroup(RhiDevice& rhiDevice) const;
    void destroyPanelGlassBindGroup() const;
    void destroyImageTextureBindings();
    void populatePanelRhiContext(UIRenderContext& context) const;
    void collectGameplayText(UIRenderContext& context);
    void collectSceneText(UIRenderContext& context);

    struct ImageTextureBinding {
        RhiTextureViewHandle view;
        RhiBindGroupHandle bindGroup;
    };

    CrosshairControl m_crosshair;
    HotbarControl m_hotbar;
    HudControl m_hud;
    UIPanel m_deathBackdrop;
    UIText m_deathTitle;
    UIText m_deathPrompt;
    InventoryPanelControl m_inventoryPanel;
    DataDrivenContainerPanelControl m_storagePanel;
    DataDrivenContainerPanelControl m_machinePanel;
    CreativeInventoryPanelControl m_creativeInventoryPanel;
    TextRenderer m_text;
    CommandInputOverlay m_commandInput;
    ConsoleOverlay m_console;
    std::vector<UIWidget*> m_widgetControls;
    GameResources* m_resources = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    RhiCommandListPool* m_commandListPool = nullptr;
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    UIScene* m_activeScene = nullptr;
    UITheme m_theme;
    const LocaleManager* m_localeManager = nullptr;
    mutable UIRenderContext m_lastSceneContext;
    int m_surfaceWidth = 1;
    int m_surfaceHeight = 1;
    bool m_commandInputRequested = false;

    // GUI Scale setting
    GUIScale m_guiScale = GUIScale::Auto;

    std::size_t m_consoleMaxLines = 64;

    mutable RhiTextureHandle m_backdropSource;
    mutable RhiTextureHandle m_backdropBlur[2];
    mutable RhiTextureViewHandle m_backdropSourceView;
    mutable RhiTextureViewHandle m_backdropBlurView[2];
    mutable RhiSamplerHandle m_backdropBlurSampler;
    mutable RhiBindGroupLayoutHandle m_backdropBlurBindGroupLayout;
    mutable RhiPipelineLayoutHandle m_backdropBlurPipelineLayout;
    mutable RhiShaderHandle m_backdropBlurVertexShader;
    mutable RhiShaderHandle m_backdropBlurFragmentShader;
    mutable RhiPipelineHandle m_backdropBlurPipeline;
    mutable RhiBindGroupHandle m_backdropBlurBindGroup[3];
    mutable RhiDevice* m_backdropRhiViewDevice = nullptr;
    mutable int m_backdropSourceWidth = 0;
    mutable int m_backdropSourceHeight = 0;
    mutable int m_backdropBlurWidth = 0;
    mutable int m_backdropBlurHeight = 0;
    mutable RhiResourceState m_backdropSourceState = RhiResourceState::Undefined;
    mutable RhiResourceState m_backdropBlurState[2] = {RhiResourceState::Undefined, RhiResourceState::Undefined};

    RhiDevice* m_panelRhiDevice = nullptr;
    RhiBufferHandle m_panelQuadVertexBuffer;
    RhiShaderHandle m_panelSolidVertexShader;
    RhiShaderHandle m_panelSolidFragmentShader;
    RhiShaderHandle m_panelGlassVertexShader;
    RhiShaderHandle m_panelGlassFragmentShader;
    RhiShaderHandle m_imageTextureVertexShader;
    RhiShaderHandle m_imageTextureFragmentShader;
    RhiBindGroupLayoutHandle m_panelGlassBindGroupLayout;
    RhiBindGroupLayoutHandle m_imageTextureBindGroupLayout;
    RhiPipelineLayoutHandle m_panelSolidPipelineLayout;
    RhiPipelineLayoutHandle m_panelGlassPipelineLayout;
    RhiPipelineLayoutHandle m_imageTexturePipelineLayout;
    RhiPipelineHandle m_panelSolidPipeline;
    RhiPipelineHandle m_panelGlassPipeline;
    RhiPipelineHandle m_imageTexturePipeline;
    RhiSamplerHandle m_panelGlassSampler;
    RhiSamplerHandle m_imageTextureSampler;
    mutable RhiBindGroupHandle m_panelGlassBindGroup;
    mutable std::unordered_map<uint64_t, ImageTextureBinding> m_imageTextureBindings;
};
