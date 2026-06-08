#include "GameManager.h"
#include "states/MainMenuAppState.h"
#include "../Paths.h"
#include "../engine/platform/Time.h"
#include "../world/block/Block.h"
#include "../item/Item.h"
#include "../net/ENetTransport.h"
#include <iostream>
#include <string>
#include <GLFW/glfw3.h>
#ifdef MECRAFT_DEBUG
#include <chrono>
#include "../../third_party/imgui/imgui_impl_glfw.h"
#endif

GameManager::GameManager() 
    : m_contextManager(m_actionMap, m_input) {
}

GameManager::~GameManager() = default;

void GameManager::init(int width, int height, const char* title) {
    if (!initWindow(width, height, title)) {
        return;
    }
    m_threadPool.start();
    initResources();
    
    m_audioEngine.init();
    m_bgmSystem.init(m_audioEngine);
    m_uiRenderer.init(m_resourceMgr);
    m_localeManager.loadSettings();
    m_uiRenderer.setLocaleManager(&m_localeManager);
    if (!net::ENetTransport::initialize()) {
        std::cerr << "Failed to initialize ENet; multiplayer connections will fail." << std::endl;
    }

    m_appStateMachine.pushState(std::make_unique<MainMenuAppState>(makeAppStateDependencies()));
}

bool GameManager::initWindow(int width, int height, const char* title) {
    if (!m_window.init(width, height, title)) {
        std::cerr << "Error while initializing the window." << std::endl;
        return false;
    }
    m_input.init(m_window.getHandle());
    m_input.captureMouse(false);
    
    m_actionMap.loadFromFile(KEYBINDINGS_PATH);
    Time::init();
    return true;
}

void GameManager::initResources() {
    m_resourceMgr.init();
    m_resourceMgr.buildTextureAtlas(BLOCKS_TEXTURES_DIR, 16);
    m_resourceMgr.preloadTextureAnimationsFromConfig(BLOCKS_CONFIG_PATH);
    m_resourceMgr.buildTextureArray(BLOCKS_TEXTURES_DIR, 16);
    m_resourceMgr.loadLightmapTextures(LIGHTMAP_DAY_PATH, LIGHTMAP_NIGHT_PATH);
    m_resourceMgr.loadColormapTextures(GRASS_TEXTURE_PATH, FOLIAGE_TEXTURE_PATH);
    m_resourceMgr.loadTexture2D("shader_noise2d", SHADERPACK_NOISE2D_PATH, false, true, true, false);
    m_resourceMgr.loadTexture2D("shader_bayer256", SHADERPACK_BAYER256_PATH, false, true, false, false);
    // DerivativeMain/texture/RippleNormal.png.mcmeta uses blur=true, clamp=false.
    m_resourceMgr.loadTexture2D("shader_ripple_normal", SHADERPACK_RIPPLE_NORMAL_PATH, false, true, true, false);
    m_resourceMgr.loadTexture2D("shader_ldr_lut", SHADERPACK_LDR_LUT_PATH, false, false, true, false);
    m_resourceMgr.loadTexture2D("rain", RAIN_TEXTURE_PATH, false, false, false, false);  // NEAREST for sharp streaks
    m_resourceMgr.loadTexture2D("snow", SNOW_TEXTURE_PATH, false, false, false, false);  // NEAREST for sharp flakes
    m_resourceMgr.probeAtmosphereLut("Transmittance", SHADERPACK_TRANSMITTANCE_LUT_PATH, 256U * 64U * 16U);
    m_resourceMgr.probeAtmosphereLut("Scattering", SHADERPACK_SCATTERING_LUT_PATH, 32U * 128U * 32U * 8U * 16U);
    m_resourceMgr.probeAtmosphereLut("Irradiance", SHADERPACK_IRRADIANCE_LUT_PATH, 64U * 16U * 16U);
    m_resourceMgr.probeAtmosphereLut("Final", SHADERPACK_FINAL_LUT_PATH);
    m_resourceMgr.buildItemTextureAtlas(ITEMS_TEXTURES_DIR, 16);
    m_resourceMgr.loadGuiTexture("widgets", WIDGETS_TEXTURE_PATH, true);
    m_resourceMgr.loadGuiTexture("inventory", INVENTORY_TEX_PATH, true);
    m_resourceMgr.loadGuiTexture("chest_generic_54", CHEST_GUI_TEX_PATH, true);
    m_resourceMgr.loadGuiTexture("creative_tab_inventory", CREATIVE_INVENTORY_PATH, true);
    m_resourceMgr.loadGuiTexture("creative_tab_items", CREATIVE_TAB_ITEMS_PATH, true);
    for (int i = 1; i <= 7; ++i) {
        const std::string suffix = std::to_string(i) + ".png";
        m_resourceMgr.loadGuiTexture("creative_tab_top_selected_" + std::to_string(i),
                                     std::string(CREATIVE_TABS_PATH) + "/tab_top_selected_" + suffix,
                                     true);
        m_resourceMgr.loadGuiTexture("creative_tab_top_unselected_" + std::to_string(i),
                                     std::string(CREATIVE_TABS_PATH) + "/tab_top_unselected_" + suffix,
                                     true);
        m_resourceMgr.loadGuiTexture("creative_tab_bottom_selected_" + std::to_string(i),
                                     std::string(CREATIVE_TABS_PATH) + "/tab_bottom_selected_" + suffix,
                                     true);
        m_resourceMgr.loadGuiTexture("creative_tab_bottom_unselected_" + std::to_string(i),
                                     std::string(CREATIVE_TABS_PATH) + "/tab_bottom_unselected_" + suffix,
                                     true);
    }
    m_resourceMgr.loadGuiTexture("creative_scroller", std::string(CREATIVE_TABS_PATH) + "/scroller.png", true);
    m_resourceMgr.loadGuiTexture("creative_scroller_disabled", std::string(CREATIVE_TABS_PATH) + "/scroller_disabled.png", true);
    m_resourceMgr.loadGuiTexture("steve", STEVE_TEXTURE_PATH, true);
    m_resourceMgr.loadGuiTexture("zombie", ZOMBIE_TEXTURE_PATH, true);

    m_resourceMgr.buildHudIconAtlas(ICONS_TEXTURE_DIR, 8);

    BlockRegistry::init(&m_resourceMgr);
    ItemRegistry::init();
    m_resourceMgr.buildBlockIconAtlas(64);
}

AppStateDependencies GameManager::makeAppStateDependencies() {
    return {
        m_appStateMachine,
        m_window,
        m_input,
        m_actionMap,
        m_contextManager,
        m_resourceMgr,
        m_audioEngine,
        m_bgmSystem,
        m_uiRenderer,
        m_localeManager,
        m_threadPool
    };
}

double GameManager::clampFrameTime(const double dt) {
    constexpr double kMaxFrameTime = 0.25;
    return dt > kMaxFrameTime ? kMaxFrameTime : dt;
}

void GameManager::run() {
    double accumulator = 0.0;
    while (!m_window.shouldClose()) {
#ifdef MECRAFT_DEBUG
        m_input.resetDebugEventStats();
        ImGui_ImplGlfw_ResetDebugPollStats();
        const auto pollStart = std::chrono::steady_clock::now();
#endif
        m_window.pollEvents();
#ifdef MECRAFT_DEBUG
        const auto pollEnd = std::chrono::steady_clock::now();
        const auto& pollEventStats = m_input.debugEventStats();
        const auto imguiPollStats = ImGui_ImplGlfw_GetDebugPollStats();
        m_appStateMachine.recordPollEvents(std::chrono::duration<double, std::milli>(pollEnd - pollStart).count(),
                                           pollEventStats.keyEvents,
                                           pollEventStats.mouseButtonEvents,
                                           pollEventStats.cursorPosEvents,
                                           pollEventStats.scrollEvents,
                                           pollEventStats.charEvents,
                                           pollEventStats.callbackMs(),
                                           pollEventStats.cursorPosCallbackMs,
                                           imguiPollStats.callbackMs,
                                           imguiPollStats.cursorPosCallbackMs,
                                           imguiPollStats.cursorPosBackendMs,
                                           imguiPollStats.wndProcMs,
                                           imguiPollStats.wndProcSlowestMs,
                                           imguiPollStats.wndProcSlowestMsg,
                                           static_cast<unsigned>(imguiPollStats.wndProcCount));
#endif
        Time::update();

        const double frameTime = clampFrameTime(Time::getRawDeltaTime());
        accumulator += frameTime;

#ifdef MECRAFT_DEBUG
        const auto updateStart = std::chrono::steady_clock::now();
#endif
        m_appStateMachine.update(frameTime, accumulator);
#ifdef MECRAFT_DEBUG
        const auto updateEnd = std::chrono::steady_clock::now();
        m_appStateMachine.recordAppUpdateDispatch(std::chrono::duration<double, std::milli>(updateEnd - updateStart).count());
        const auto renderStart = std::chrono::steady_clock::now();
#endif
        m_appStateMachine.render(frameTime);
#ifdef MECRAFT_DEBUG
        const auto renderEnd = std::chrono::steady_clock::now();
        m_appStateMachine.recordAppRenderDispatch(std::chrono::duration<double, std::milli>(renderEnd - renderStart).count());
#endif
    }
}

void GameManager::shutdown() {
    while (!m_appStateMachine.isEmpty()) {
        m_appStateMachine.popState();
    }
    m_uiRenderer.shutdown();
    m_bgmSystem.shutdown();
    m_audioEngine.shutdown();
    net::ENetTransport::deinitialize();
    m_threadPool.shutdown();
}
