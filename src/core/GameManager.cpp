#include "GameManager.h"
#include "app_states/MainMenuAppState.h"
#include "Paths.h"
#include "Time.h"
#include "../world/Block.h"
#include "../item/Item.h"
#include <iostream>
#include <string>
#include <GLFW/glfw3.h>

GameManager::GameManager() 
    : m_contextManager(m_actionMap, m_input) {
}

GameManager::~GameManager() = default;

void GameManager::init(int width, int height, const char* title) {
    if (!initWindow(width, height, title)) {
        return;
    }
    initResources();
    
    m_audioEngine.init();
    m_bgmSystem.init(m_audioEngine);
    m_uiRenderer.init(m_resourceMgr);
    m_localeManager.loadSettings();
    m_uiRenderer.setLocaleManager(&m_localeManager);

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
    m_resourceMgr.buildItemTextureAtlas(ITEMS_TEXTURES_DIR, 16);
    m_resourceMgr.loadGuiTexture("widgets", WIDGETS_TEXTURE_PATH, true);
    m_resourceMgr.loadGuiTexture("inventory", INVENTORY_TEX_PATH, true);
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
    m_resourceMgr.loadGuiTexture("sun", SUN_TEXTURE_PATH, false);
    m_resourceMgr.loadGuiTexture("moon_phases", MOON_TEXTURE_PATH, false);

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
        m_localeManager
    };
}

double GameManager::clampFrameTime(const double dt) {
    constexpr double kMaxFrameTime = 0.25;
    return dt > kMaxFrameTime ? kMaxFrameTime : dt;
}

void GameManager::run() {
    double accumulator = 0.0;
    while (!m_window.shouldClose()) {
        m_window.pollEvents();
        Time::update();

        const double frameTime = clampFrameTime(Time::deltaTime);
        accumulator += frameTime;

        m_appStateMachine.update(frameTime, accumulator);
        m_appStateMachine.render(frameTime);
    }
}

void GameManager::shutdown() {
    while (!m_appStateMachine.isEmpty()) {
        m_appStateMachine.popState();
    }
    m_uiRenderer.shutdown();
    m_bgmSystem.shutdown();
    m_audioEngine.shutdown();
}
