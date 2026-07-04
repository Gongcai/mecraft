#include "GameManager.h"
#include "AppSettings.h"
#include "GameResourceBootstrapper.h"
#include "states/LoadingAppState.h"
#include "states/MainMenuAppState.h"
#include "../Diagnostics.h"
#include "../Paths.h"
#include "../engine/platform/Time.h"
#include "../save/SaveManager.h"
#include "../net/ENetTransport.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
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

bool GameManager::init(int width, int height, const char* title, AppLaunchOptions launchOptions) {
    m_launchOptions = std::move(launchOptions);
    if (!initWindow(width, height, title)) {
        return false;
    }
    m_threadPool.start();
    app::bootstrapGameResources(m_resourceMgr);
    
    m_audioEngine.init();
    m_bgmSystem.init(m_audioEngine);
    m_uiRenderer.init(m_resourceMgr);
    m_localeManager.loadSettings();
    m_uiRenderer.setLocaleManager(&m_localeManager);
    if (!net::ENetTransport::initialize()) {
        MECRAFT_LOG_STREAM(std::cerr << "Failed to initialize ENet; multiplayer connections will fail." << std::endl);
    }

    if (!configureInputReplay()) {
        return false;
    }

    if (m_launchOptions.autoStartGameplay) {
        GameSessionConfig benchmarkConfig;
        if (!makeBenchmarkSessionConfig(benchmarkConfig)) {
            return false;
        }
        m_appStateMachine.pushState(std::make_unique<LoadingAppState>(makeAppStateDependencies(),
                                                                      std::move(benchmarkConfig)));
    } else {
        m_appStateMachine.pushState(std::make_unique<MainMenuAppState>(makeAppStateDependencies()));
    }

    if (m_launchOptions.inputReplayScope == AppLaunchOptions::InputReplayScope::App) {
        activateInputReplayForScope(AppLaunchOptions::InputReplayScope::App);
    }
    return true;
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

bool GameManager::makeBenchmarkSessionConfig(GameSessionConfig& outConfig) const {
    outConfig = GameSessionConfig{};
    outConfig.seed = m_launchOptions.benchmarkSeed;
    outConfig.renderDistance = m_launchOptions.benchmarkRenderDistanceSet
        ? m_launchOptions.benchmarkRenderDistance
        : app::loadRenderDistance();
    outConfig.worldName = m_launchOptions.benchmarkWorldName;
    outConfig.worldDisplayName = m_launchOptions.benchmarkWorldDisplayName.empty()
        ? m_launchOptions.benchmarkWorldName
        : m_launchOptions.benchmarkWorldDisplayName;
    outConfig.saveRoot = m_launchOptions.benchmarkSaveRoot;
    outConfig.enableSaving = m_launchOptions.benchmarkEnableSaving;

    if (!m_launchOptions.benchmarkSeedSet && !outConfig.worldName.empty()) {
        const std::filesystem::path worldPath = outConfig.saveRoot / outConfig.worldName;
        if (std::filesystem::exists(worldPath)) {
            save::SaveManager saveManager(worldPath);
            save::LevelMeta meta;
            if (!saveManager.loadLevelMeta(meta)) {
                MECRAFT_LOG_STREAM(std::cerr << "Failed to read benchmark world metadata: "
                                  << worldPath.string() << '\n');
                return false;
            }
            outConfig.seed = static_cast<int>(meta.seed);
        }
    }
    return true;
}

bool GameManager::configureInputReplay() {
    if (m_launchOptions.recordInput && m_launchOptions.replayInput) {
        MECRAFT_LOG_STREAM(std::cerr << "Input recording and playback cannot be enabled at the same time\n");
        return false;
    }
    if (m_launchOptions.recordInput) {
        m_input.configureInputRecording(m_launchOptions.inputRecordPath);
    }
    if (m_launchOptions.replayInput) {
        m_input.configureInputPlayback(m_launchOptions.inputReplayPath);
    }
    return true;
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
