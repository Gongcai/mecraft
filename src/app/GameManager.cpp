#include "GameManager.h"
#include "AppSettings.h"
#include "states/LoadingAppState.h"
#include "states/MainMenuAppState.h"
#include "../Diagnostics.h"
#include "../Paths.h"
#include "../engine/platform/Time.h"
#include "../save/SaveManager.h"
#include "../world/block/Block.h"
#include "../item/Item.h"
#include "../net/ENetTransport.h"
#include "../ui/inventory/ContainerUiRegistry.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#ifdef MECRAFT_DEBUG
#include <chrono>
#include "../../third_party/imgui/imgui_impl_glfw.h"
#endif

namespace {

double percentileFromSorted(const std::vector<double>& sortedValues, const double percentile) {
    const double rank = std::ceil((percentile / 100.0) * static_cast<double>(sortedValues.size()));
    const auto clampedRank = static_cast<size_t>(std::clamp(rank, 1.0, static_cast<double>(sortedValues.size())));
    return sortedValues[clampedRank - 1];
}

} // namespace

GameManager::GameManager() 
    : m_contextManager(m_actionMap, m_input) {
}

GameManager::~GameManager() = default;

void GameManager::init(int width, int height, const char* title, AppLaunchOptions launchOptions) {
    m_launchOptions = std::move(launchOptions);
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
        MECRAFT_LOG_STREAM(std::cerr << "Failed to initialize ENet; multiplayer connections will fail." << std::endl);
    }

    configureInputReplay();

    if (m_launchOptions.autoStartGameplay) {
        m_appStateMachine.pushState(std::make_unique<LoadingAppState>(makeAppStateDependencies(),
                                                                      makeBenchmarkSessionConfig()));
    } else {
        m_appStateMachine.pushState(std::make_unique<MainMenuAppState>(makeAppStateDependencies()));
    }

    if (m_launchOptions.inputReplayScope == AppLaunchOptions::InputReplayScope::App) {
        activateInputReplayForScope(AppLaunchOptions::InputReplayScope::App);
    }
}

bool GameManager::initWindow(int width, int height, const char* title) {
    if (!m_window.init(width, height, title, m_launchOptions.enableGlDebugOutput)) {
        MECRAFT_LOG_STREAM(std::cerr << "Error while initializing the window." << std::endl);
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
    m_resourceMgr.loadBlockTextureCatalog(BLOCK_TEXTURES_CONFIG_PATH);
    m_resourceMgr.buildTextureAtlas(BLOCKS_TEXTURES_DIR, 16);
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
    ui::ContainerUiRegistry::init();
    for (const auto& [id, def] : ui::ContainerUiRegistry::all()) {
        const std::string texturePath = std::string(ASSETS_DIR) + "/" + def.backgroundTexturePath;
        if (m_resourceMgr.loadGuiTexture(def.backgroundTexture, texturePath, true) == 0) {
            throw std::runtime_error("Failed to load container UI texture for " + id + ": " + texturePath);
        }
    }
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
    m_resourceMgr.loadGuiTexture("chest", CHEST_ENTITY_TEXTURE_PATH, true);
    m_resourceMgr.preloadEntityTexturesFromConfig(ENTITIES_CONFIG_PATH);

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
        m_threadPool,
        m_launchOptions.enableDebugDashboard,
        [this]() { activateInputReplayForScope(AppLaunchOptions::InputReplayScope::Gameplay); },
        [this]() {
            if (m_launchOptions.inputReplayScope == AppLaunchOptions::InputReplayScope::Gameplay) {
                m_input.setInputReplayActive(false);
            }
        },
        [this]() {
            return m_launchOptions.inputReplayScope == AppLaunchOptions::InputReplayScope::Gameplay &&
                   (m_launchOptions.recordInput || m_launchOptions.replayInput);
        }
    };
}

GameSessionConfig GameManager::makeBenchmarkSessionConfig() const {
    GameSessionConfig config;
    config.seed = m_launchOptions.benchmarkSeed;
    config.renderDistance = m_launchOptions.benchmarkRenderDistanceSet
        ? m_launchOptions.benchmarkRenderDistance
        : app::loadRenderDistance();
    config.worldName = m_launchOptions.benchmarkWorldName;
    config.worldDisplayName = m_launchOptions.benchmarkWorldDisplayName.empty()
        ? m_launchOptions.benchmarkWorldName
        : m_launchOptions.benchmarkWorldDisplayName;
    config.saveRoot = m_launchOptions.benchmarkSaveRoot;
    config.enableSaving = m_launchOptions.benchmarkEnableSaving;

    if (!m_launchOptions.benchmarkSeedSet && !config.worldName.empty()) {
        const std::filesystem::path worldPath = config.saveRoot / config.worldName;
        if (std::filesystem::exists(worldPath)) {
            save::SaveManager saveManager(worldPath);
            save::LevelMeta meta;
            if (!saveManager.loadLevelMeta(meta)) {
                throw std::runtime_error("Failed to read benchmark world metadata: " + worldPath.string());
            }
            config.seed = static_cast<int>(meta.seed);
        }
    }
    return config;
}

void GameManager::configureInputReplay() {
    if (m_launchOptions.recordInput && m_launchOptions.replayInput) {
        throw std::runtime_error("Input recording and playback cannot be enabled at the same time");
    }
    if (m_launchOptions.recordInput) {
        m_input.configureInputRecording(m_launchOptions.inputRecordPath);
    }
    if (m_launchOptions.replayInput) {
        m_input.configureInputPlayback(m_launchOptions.inputReplayPath);
    }
}

void GameManager::activateInputReplayForScope(const AppLaunchOptions::InputReplayScope scope) {
    if (m_launchOptions.inputReplayScope != scope) {
        return;
    }
    m_input.setInputReplayActive(true);
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
        recordBenchmarkFrame(frameTime);
        closeWindowIfBenchmarkComplete();
    }
}

void GameManager::recordBenchmarkFrame(const double frameTime) {
    if (!m_launchOptions.autoStartGameplay || !m_input.isInputReplayActive()) {
        m_benchmarkReplayWasActive = false;
        return;
    }
    if (!m_benchmarkReplayWasActive) {
        m_benchmarkReplayWasActive = true;
        return;
    }

    const double frameMs = frameTime * 1000.0;
    if (!m_benchmarkStats.active) {
        m_benchmarkStats.active = true;
        m_benchmarkStats.minFrameMs = frameMs;
        m_benchmarkStats.maxFrameMs = frameMs;
        if (m_launchOptions.benchmarkDurationSeconds > 0.0) {
            m_benchmarkStats.frameTimesMs.reserve(
                static_cast<size_t>(std::ceil(m_launchOptions.benchmarkDurationSeconds * 240.0)));
        }
    }

    ++m_benchmarkStats.frameCount;
    m_benchmarkStats.replayActiveSeconds = m_input.inputReplayActiveSeconds();
    m_benchmarkStats.totalFrameMs += frameMs;
    m_benchmarkStats.minFrameMs = std::min(m_benchmarkStats.minFrameMs, frameMs);
    m_benchmarkStats.maxFrameMs = std::max(m_benchmarkStats.maxFrameMs, frameMs);
    m_benchmarkStats.frameTimesMs.push_back(frameMs);
}

void GameManager::closeWindowIfBenchmarkComplete() {
    if (!m_input.isInputReplayActive()) {
        return;
    }
    if (m_launchOptions.benchmarkDurationSeconds > 0.0 &&
        m_input.inputReplayActiveSeconds() >= m_launchOptions.benchmarkDurationSeconds) {
        glfwSetWindowShouldClose(m_window.getHandle(), true);
        return;
    }
    if (m_launchOptions.replayInput &&
        m_launchOptions.exitWhenPlaybackEnds &&
        m_input.isInputPlaybackFinished()) {
        glfwSetWindowShouldClose(m_window.getHandle(), true);
    }
}

void GameManager::writeBenchmarkReport() {
    if (m_benchmarkReportWritten) {
        return;
    }
    m_benchmarkReportWritten = true;
    if (!m_benchmarkStats.active || m_benchmarkStats.frameCount == 0) {
        return;
    }

    std::vector<double> sortedFrameMs = m_benchmarkStats.frameTimesMs;
    std::sort(sortedFrameMs.begin(), sortedFrameMs.end());

    const double frameCount = static_cast<double>(m_benchmarkStats.frameCount);
    const double avgFrameMs = m_benchmarkStats.totalFrameMs / frameCount;
    const double medianFrameMs = percentileFromSorted(sortedFrameMs, 50.0);
    const double p95FrameMs = percentileFromSorted(sortedFrameMs, 95.0);
    const double p99FrameMs = percentileFromSorted(sortedFrameMs, 99.0);
    const double avgFps = avgFrameMs > 0.0 ? 1000.0 / avgFrameMs : 0.0;

    std::cout << std::fixed << std::setprecision(3)
              << "[Benchmark] frames=" << m_benchmarkStats.frameCount
              << " replay_active_s=" << m_benchmarkStats.replayActiveSeconds
              << " avg_ms=" << avgFrameMs
              << " median_ms=" << medianFrameMs
              << " p95_ms=" << p95FrameMs
              << " p99_ms=" << p99FrameMs
              << " min_ms=" << m_benchmarkStats.minFrameMs
              << " max_ms=" << m_benchmarkStats.maxFrameMs
              << " avg_fps=" << avgFps
              << '\n';

    if (m_launchOptions.benchmarkReportPath.empty()) {
        return;
    }

    nlohmann::json root;
    root["kind"] = "mecraft.benchmark_frame_report";
    root["world"] = m_launchOptions.benchmarkWorldName;
    root["replay_input"] = m_launchOptions.inputReplayPath.string();
    root["benchmark_duration_seconds"] = m_launchOptions.benchmarkDurationSeconds;
    root["replay_active_seconds"] = m_benchmarkStats.replayActiveSeconds;
    root["frame_count"] = m_benchmarkStats.frameCount;
    root["frame_ms"] = {
        {"average", avgFrameMs},
        {"median", medianFrameMs},
        {"p95", p95FrameMs},
        {"p99", p99FrameMs},
        {"min", m_benchmarkStats.minFrameMs},
        {"max", m_benchmarkStats.maxFrameMs}
    };
    root["fps"] = {
        {"average", avgFps}
    };

    const std::filesystem::path reportPath = m_launchOptions.benchmarkReportPath;
    const std::filesystem::path parentPath = reportPath.parent_path();
    if (!parentPath.empty()) {
        std::error_code createError;
        std::filesystem::create_directories(parentPath, createError);
        if (createError) {
            std::cerr << "[Benchmark] Failed to create report directory: "
                      << parentPath << ": " << createError.message() << '\n';
            return;
        }
    }

    std::ofstream output(reportPath);
    if (!output) {
        std::cerr << "[Benchmark] Failed to write report: " << reportPath << '\n';
        return;
    }
    output << root.dump(2);
    std::cout << "[Benchmark] Report written to " << reportPath << '\n';
}

void GameManager::shutdown() {
    writeBenchmarkReport();
    while (!m_appStateMachine.isEmpty()) {
        m_appStateMachine.popState();
    }
    m_uiRenderer.shutdown();
    m_bgmSystem.shutdown();
    m_audioEngine.shutdown();
    net::ENetTransport::deinitialize();
    m_threadPool.shutdown();
    m_input.shutdownInputReplay();
}
