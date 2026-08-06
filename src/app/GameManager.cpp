#include "GameManager.h"
#include "AppSettings.h"
#include "GameResourceBootstrapper.h"
#include "states/LoadingAppState.h"
#include "states/MainMenuAppState.h"
#include "states/ModelSceneAppState.h"
#include "../Diagnostics.h"
#include "../Paths.h"
#include "../engine/platform/Time.h"
#include "../renderer/rhi/RhiDevice.h"
#include "../renderer/rhi/RhiDeviceFactory.h"
#include "../renderer/rhi/RhiCommandListPool.h"
#if defined(MECRAFT_ENABLE_STREAMLINE)
#include "../renderer/upscaling/StreamlineRuntime.h"
#endif
#include "../save/SaveManager.h"
#include "../net/ENetTransport.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <GLFW/glfw3.h>
#if defined(MECRAFT_ENABLE_STREAMLINE) && defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <nlohmann/json.hpp>
#ifdef MECRAFT_DEBUG
#include "../../third_party/imgui/imgui_impl_glfw.h"
#endif

namespace {

double percentileFromSorted(const std::vector<double>& sortedValues, const double percentile) {
    const double rank = std::ceil((percentile / 100.0) * static_cast<double>(sortedValues.size()));
    const auto clampedRank = static_cast<size_t>(std::clamp(rank, 1.0, static_cast<double>(sortedValues.size())));
    return sortedValues[clampedRank - 1];
}

nlohmann::json gpuTimingPercentilesJson(const GpuTimingPercentiles& timing) {
    return {{"p50", timing.p50Ms}, {"p95", timing.p95Ms}, {"p99", timing.p99Ms}};
}

nlohmann::json rhiMemoryStatsJson(const RhiMemoryStats& stats) {
    nlohmann::json categories = nlohmann::json::object();
    for (size_t index = 0u; index < kRhiMemoryCategoryCount; ++index) {
        const auto category = static_cast<RhiMemoryCategory>(index);
        const RhiMemoryCategoryStats& entry = stats.categories[index];
        categories[rhiMemoryCategoryStableId(category)] = {{"bytes", entry.bytes},
                                                           {"allocation_count", entry.allocationCount},
                                                           {"resource_count", entry.resourceCount}};
    }
    return {{"valid", stats.valid},
            {"accuracy", rhiMemoryStatsAccuracyStableId(stats.accuracy)},
            {"total_bytes", stats.totalBytes},
            {"total_allocation_count", stats.totalAllocationCount},
            {"total_resource_count", stats.totalResourceCount},
            {"categories", std::move(categories)}};
}

} // namespace

GameManager::GameManager() : m_contextManager(m_actionMap, m_input) {}

GameManager::~GameManager() = default;

bool GameManager::init(int width, int height, const char* title, AppLaunchOptions launchOptions) {
    m_launchOptions = std::move(launchOptions);
    m_benchmarkStats = {};
    m_benchmarkGpuTimingHistory.reset();
    m_benchmarkReplayWasActive = false;
    m_benchmarkReportWritten = false;
    m_benchmarkReportSucceeded = true;
    m_runSucceeded = true;
    if (!m_validationRun.configure(m_launchOptions)) {
        MECRAFT_LOG_STREAM(std::cerr << "GameManager: validation configuration failed: "
                                     << app::validation::validationRunErrorStableId(m_validationRun.error()) << ":"
                                     << m_validationRun.detail() << '\n');
        return false;
    }
    std::optional<RhiBackend> savedBackend;
    if (!m_launchOptions.rhiBackendExplicit && !m_launchOptions.validationEnabled()) {
        const app::RhiBackendSettingResult backendSetting = app::loadRhiBackend();
        if (!backendSetting.isValid) {
            MECRAFT_LOG_STREAM(std::cerr << "GameManager: invalid app.rhiBackend setting\n");
            return false;
        }
        savedBackend = backendSetting.backend;
    }
    m_launchOptions.rhiBackend = resolveLaunchRhiBackend(m_launchOptions, savedBackend);
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (m_launchOptions.rhiBackend == RhiBackend::Vulkan) {
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        if (!streamline.initialize()) {
            MECRAFT_LOG_STREAM(std::cerr << streamline.lastError() << '\n');
            return false;
        }
    }
#endif
    if (m_launchOptions.validationEnabled()) {
        m_vsyncEnabled = false;
        m_fullscreenEnabled = false;
    } else {
        const app::VsyncSettingResult vsyncSetting = app::loadVsyncEnabled();
        if (!vsyncSetting.isValid) {
            MECRAFT_LOG_STREAM(std::cerr << "GameManager: invalid app.vsyncEnabled setting\n");
            return false;
        }
        m_vsyncEnabled = vsyncSetting.enabled;
        const app::FullscreenSettingResult fullscreenSetting = app::loadFullscreenEnabled();
        if (!fullscreenSetting.isValid) {
            MECRAFT_LOG_STREAM(std::cerr << "GameManager: invalid app.fullscreenEnabled setting\n");
            return false;
        }
        m_fullscreenEnabled = fullscreenSetting.enabled.has_value() && *fullscreenSetting.enabled;
    }
    m_rhiDevice = renderer::rhi::createRhiDevice(m_launchOptions.rhiBackend);
    if (!m_rhiDevice) {
        MECRAFT_LOG_STREAM(std::cerr << "GameManager: requested RHI backend is unavailable: "
                                     << renderer::rhi::rhiBackendDisplayName(m_launchOptions.rhiBackend) << '\n');
        return false;
    }
    if (!initWindow(width, height, title)) {
        m_rhiDevice.reset();
        return false;
    }
    if (m_fullscreenEnabled && !m_window.setFullscreen(true)) {
        MECRAFT_LOG_STREAM(std::cerr << "GameManager: failed to enter configured fullscreen mode\n");
        return false;
    }
    if (!initRhiDevice()) {
        return false;
    }
#if defined(MECRAFT_ENABLE_STREAMLINE) && defined(_WIN32)
    if (m_launchOptions.rhiBackend == RhiBackend::Vulkan) {
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        if (!streamline.attachLatencyWindow(glfwGetWin32Window(m_window.getHandle()))) {
            MECRAFT_LOG_STREAM(std::cerr << streamline.lastError() << '\n');
            return false;
        }
    }
#endif
    m_threadPool.start();
    if (!app::bootstrapGameResources(m_resourceMgr, *m_rhiDevice, *m_commandListPool)) {
        return false;
    }

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

    if (m_validationRun.scene() == ValidationScene::Model) {
        m_appStateMachine.pushState(std::make_unique<ModelSceneAppState>(makeAppStateDependencies()));
    } else if (m_validationRun.scene() == ValidationScene::Voxel || m_launchOptions.autoStartGameplay) {
        GameSessionConfig benchmarkConfig;
        if (!makeBenchmarkSessionConfig(benchmarkConfig)) {
            return false;
        }
        m_appStateMachine.pushState(
            std::make_unique<LoadingAppState>(makeAppStateDependencies(), std::move(benchmarkConfig)));
    } else {
        m_appStateMachine.pushState(std::make_unique<MainMenuAppState>(makeAppStateDependencies()));
    }

    if (m_launchOptions.inputReplayScope == AppLaunchOptions::InputReplayScope::App) {
        activateInputReplayForScope(AppLaunchOptions::InputReplayScope::App);
    }
    return true;
}

bool GameManager::initRhiDevice() {
    RhiDeviceDesc desc;
    desc.debugName = "AppRenderer";
    desc.nativeWindow = m_window.getHandle();
    desc.width = m_window.getWidth();
    desc.height = m_window.getHeight();
    desc.enableDebugOutput = m_launchOptions.enableRhiDebugOutput;
    desc.vsyncEnabled = m_vsyncEnabled;
    if (!m_rhiDevice->init(desc)) {
        MECRAFT_LOG_STREAM(std::cerr << "GameManager: failed to initialize app RHI device\n");
        m_rhiDevice.reset();
        return false;
    }
    RhiCommandListPoolDesc poolDesc;
    poolDesc.debugName = "App.GraphicsCommandListPool";
    poolDesc.initialCommandListCapacity = 16u;
    m_commandListPool = m_rhiDevice->createCommandListPool(poolDesc);
    if (!m_commandListPool) {
        MECRAFT_LOG_STREAM(std::cerr << "GameManager: failed to create app command-list pool\n");
        m_rhiDevice->shutdown();
        m_rhiDevice.reset();
        return false;
    }
    return true;
}

bool GameManager::initWindow(int width, int height, const char* title) {
    if (!m_window.initializePlatform()) {
        MECRAFT_LOG_STREAM(std::cerr << "Error while initializing the window platform." << std::endl);
        return false;
    }
    if (!m_rhiDevice->prepareWindowCreation()) {
        MECRAFT_LOG_STREAM(std::cerr << "RHI backend failed to prepare native window creation." << std::endl);
        return false;
    }
    if (!m_window.create(width, height, title)) {
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
    return {m_appStateMachine,
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
            *m_rhiDevice,
            *m_commandListPool,
            m_validationRun,
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
            }};
}

bool GameManager::makeBenchmarkSessionConfig(GameSessionConfig& outConfig) const {
    outConfig = GameSessionConfig{};
    if (m_launchOptions.validationEnabled()) {
        const app::validation::ValidationSceneContract& contract = m_validationRun.sceneContract();
        if (contract.scene != ValidationScene::Voxel || !contract.voxelWorld.has_value()) {
            MECRAFT_LOG_STREAM(std::cerr << "Voxel validation requires one verified world identity\n");
            return false;
        }
        outConfig.seed = contract.voxelWorld->seed;
        outConfig.renderDistance = contract.voxelWorld->renderDistance;
        outConfig.enableSaving = false;
        outConfig.renderSettingsSource = GameRenderSettingsSource::FixedProfile;
        outConfig.fixedRenderSettings = m_validationRun.renderSettingsProfile().settings;
        return true;
    }

    outConfig.seed = m_launchOptions.benchmarkSeed;
    outConfig.renderDistance = m_launchOptions.benchmarkRenderDistanceSet ? m_launchOptions.benchmarkRenderDistance
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
                MECRAFT_LOG_STREAM(std::cerr << "Failed to read benchmark world metadata: " << worldPath.string()
                                             << '\n');
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
        if (!m_input.configureInputRecording(m_launchOptions.inputRecordPath)) {
            return false;
        }
    }
    if (m_launchOptions.replayInput) {
        if (!m_input.configureInputPlayback(m_launchOptions.inputReplayPath)) {
            return false;
        }
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

bool GameManager::run() {
    double accumulator = 0.0;
    while (!m_window.shouldClose()) {
        Time::beginFrame();
#if defined(MECRAFT_ENABLE_STREAMLINE)
        const uint32_t trackingFrameIndex = Time::getFrameIndex();
        StreamlineRuntime* streamline = nullptr;
        if (m_launchOptions.rhiBackend == RhiBackend::Vulkan) {
            streamline = &StreamlineRuntime::instance();
            if (!streamline->beginReflexFrame(trackingFrameIndex)) {
                MECRAFT_LOG_STREAM(std::cerr << streamline->lastError() << '\n');
                m_runSucceeded = false;
                break;
            }
        }
#endif
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
        m_appStateMachine.recordPollEvents(
            std::chrono::duration<double, std::milli>(pollEnd - pollStart).count(), pollEventStats.keyEvents,
            pollEventStats.mouseButtonEvents, pollEventStats.cursorPosEvents, pollEventStats.scrollEvents,
            pollEventStats.charEvents, pollEventStats.callbackMs(), pollEventStats.cursorPosCallbackMs,
            imguiPollStats.callbackMs, imguiPollStats.cursorPosCallbackMs, imguiPollStats.cursorPosBackendMs,
            imguiPollStats.wndProcMs, imguiPollStats.wndProcSlowestMs, imguiPollStats.wndProcSlowestMsg,
            static_cast<unsigned>(imguiPollStats.wndProcCount));
#endif
        Time::update();

#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (streamline != nullptr &&
            !streamline->setPclMarker(trackingFrameIndex, StreamlinePclMarker::SimulationStart)) {
            MECRAFT_LOG_STREAM(std::cerr << streamline->lastError() << '\n');
            m_runSucceeded = false;
            break;
        }
#endif

        const double frameTime = clampFrameTime(Time::getRawDeltaTime());
        accumulator += frameTime;

        const auto appFrameStart = std::chrono::steady_clock::now();
#ifdef MECRAFT_DEBUG
        const auto updateStart = appFrameStart;
#endif
        m_appStateMachine.update(frameTime, accumulator);
#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (streamline != nullptr &&
            !streamline->setPclMarker(trackingFrameIndex, StreamlinePclMarker::SimulationEnd)) {
            MECRAFT_LOG_STREAM(std::cerr << streamline->lastError() << '\n');
            m_runSucceeded = false;
            break;
        }
#endif
#ifdef MECRAFT_DEBUG
        const auto updateEnd = std::chrono::steady_clock::now();
        m_appStateMachine.recordAppUpdateDispatch(
            std::chrono::duration<double, std::milli>(updateEnd - updateStart).count());
        const auto renderStart = std::chrono::steady_clock::now();
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (streamline != nullptr &&
            !streamline->setPclMarker(trackingFrameIndex, StreamlinePclMarker::RenderSubmitStart)) {
            MECRAFT_LOG_STREAM(std::cerr << streamline->lastError() << '\n');
            m_runSucceeded = false;
            break;
        }
#endif
        m_appStateMachine.render(frameTime);
        const auto appFrameEnd = std::chrono::steady_clock::now();
#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (streamline != nullptr &&
            !streamline->setPclMarker(trackingFrameIndex, StreamlinePclMarker::RenderSubmitEnd)) {
            MECRAFT_LOG_STREAM(std::cerr << streamline->lastError() << '\n');
            m_runSucceeded = false;
            break;
        }
#endif
#ifdef MECRAFT_DEBUG
        const auto renderEnd = std::chrono::steady_clock::now();
        m_appStateMachine.recordAppRenderDispatch(
            std::chrono::duration<double, std::milli>(renderEnd - renderStart).count());
#endif
        const bool validationSampleCompleted = m_validationRun.consumeCompletedSampleFrame();
        const double measuredFrameSeconds =
            m_validationRun.enabled() ? std::chrono::duration<double>(appFrameEnd - appFrameStart).count() : frameTime;
        recordBenchmarkFrame(measuredFrameSeconds, validationSampleCompleted);
        closeWindowIfBenchmarkComplete();
    }
    if (!writeBenchmarkReport()) {
        m_runSucceeded = false;
    }
    return m_runSucceeded && !m_validationRun.failed() && (!m_validationRun.enabled() || m_validationRun.complete());
}

void GameManager::recordBenchmarkFrame(const double measuredFrameSeconds, const bool validationSampleCompleted) {
    if (m_validationRun.enabled()) {
        if (!validationSampleCompleted) {
            return;
        }
    } else {
        if (!m_launchOptions.autoStartGameplay || !m_input.isInputReplayActive()) {
            m_benchmarkReplayWasActive = false;
            return;
        }
        if (!m_benchmarkReplayWasActive) {
            m_benchmarkReplayWasActive = true;
            return;
        }
    }

    const double frameMs = measuredFrameSeconds * 1000.0;
    if (!m_benchmarkStats.active) {
        m_benchmarkStats.active = true;
        m_benchmarkStats.minFrameMs = frameMs;
        m_benchmarkStats.maxFrameMs = frameMs;
        if (m_validationRun.enabled()) {
            m_benchmarkStats.frameTimesMs.reserve(m_launchOptions.validationSampleFrames);
        } else if (m_launchOptions.benchmarkDurationSeconds > 0.0) {
            m_benchmarkStats.frameTimesMs.reserve(
                static_cast<size_t>(std::ceil(m_launchOptions.benchmarkDurationSeconds * 240.0)));
        }
    }

    ++m_benchmarkStats.frameCount;
    m_benchmarkStats.replayActiveSeconds = m_validationRun.enabled()
                                               ? static_cast<double>(m_validationRun.completedSampleFrames()) *
                                                     static_cast<double>(app::validation::kValidationFrameDeltaSeconds)
                                               : m_input.inputReplayActiveSeconds();
    m_benchmarkStats.totalFrameMs += frameMs;
    m_benchmarkStats.minFrameMs = std::min(m_benchmarkStats.minFrameMs, frameMs);
    m_benchmarkStats.maxFrameMs = std::max(m_benchmarkStats.maxFrameMs, frameMs);
    m_benchmarkStats.frameTimesMs.push_back(frameMs);

    const GpuFrameStats* gpuStats = m_appStateMachine.gpuFrameStats();
    if (gpuStats != nullptr) {
        (void)m_benchmarkGpuTimingHistory.record(*gpuStats);
    }
}

void GameManager::closeWindowIfBenchmarkComplete() {
    if (m_validationRun.failed()) {
        MECRAFT_LOG_STREAM(std::cerr << "[Validation] "
                                     << app::validation::validationRunErrorStableId(m_validationRun.error()) << ":"
                                     << m_validationRun.detail() << '\n');
        m_runSucceeded = false;
        glfwSetWindowShouldClose(m_window.getHandle(), true);
        return;
    }
    if (m_validationRun.complete()) {
        if (!writeBenchmarkReport()) {
            m_runSucceeded = false;
        }
        glfwSetWindowShouldClose(m_window.getHandle(), true);
        return;
    }
    if (!m_input.isInputReplayActive()) {
        return;
    }
    if (m_launchOptions.benchmarkDurationSeconds > 0.0 &&
        m_input.inputReplayActiveSeconds() >= m_launchOptions.benchmarkDurationSeconds) {
        glfwSetWindowShouldClose(m_window.getHandle(), true);
        return;
    }
    if (m_launchOptions.replayInput && m_launchOptions.exitWhenPlaybackEnds && m_input.isInputPlaybackFinished()) {
        glfwSetWindowShouldClose(m_window.getHandle(), true);
    }
}

bool GameManager::writeBenchmarkReport() {
    if (m_benchmarkReportWritten) {
        return m_benchmarkReportSucceeded;
    }
    m_benchmarkReportWritten = true;
    if (m_validationRun.enabled() && !m_validationRun.complete()) {
        m_benchmarkReportSucceeded = false;
        return false;
    }
    if (!m_benchmarkStats.active || m_benchmarkStats.frameCount == 0) {
        m_benchmarkReportSucceeded = !m_validationRun.enabled() && m_launchOptions.benchmarkReportPath.empty();
        return m_benchmarkReportSucceeded;
    }

    std::vector<double> sortedFrameMs = m_benchmarkStats.frameTimesMs;
    std::sort(sortedFrameMs.begin(), sortedFrameMs.end());

    const double frameCount = static_cast<double>(m_benchmarkStats.frameCount);
    const double avgFrameMs = m_benchmarkStats.totalFrameMs / frameCount;
    const double p50FrameMs = percentileFromSorted(sortedFrameMs, 50.0);
    const double p95FrameMs = percentileFromSorted(sortedFrameMs, 95.0);
    const double p99FrameMs = percentileFromSorted(sortedFrameMs, 99.0);
    const double avgFps = avgFrameMs > 0.0 ? 1000.0 / avgFrameMs : 0.0;
    const GpuTimingWindowStats gpuTimingWindow = m_benchmarkGpuTimingHistory.snapshot();
    const RhiMemoryStats memoryStats = m_rhiDevice != nullptr ? m_rhiDevice->memoryStats() : RhiMemoryStats{};

    std::cout << std::fixed << std::setprecision(3) << "[Benchmark] frames=" << m_benchmarkStats.frameCount
              << " replay_active_s=" << m_benchmarkStats.replayActiveSeconds
              << (m_validationRun.enabled() ? " cpu_update_render_avg_ms=" : " avg_ms=") << avgFrameMs
              << " p50_ms=" << p50FrameMs << " p95_ms=" << p95FrameMs << " p99_ms=" << p99FrameMs
              << " min_ms=" << m_benchmarkStats.minFrameMs << " max_ms=" << m_benchmarkStats.maxFrameMs
              << " avg_fps=" << avgFps << " gpu_samples=" << gpuTimingWindow.sampleCount
              << " rhi_memory_bytes=" << memoryStats.totalBytes;
    if (gpuTimingWindow.valid) {
        std::cout << " gpu_tracked_p95_ms=" << gpuTimingWindow.totalTrackedGpuMs.p95Ms;
    }
    std::cout << '\n';

    const std::filesystem::path reportPath =
        m_validationRun.enabled() ? m_launchOptions.validationReportPath : m_launchOptions.benchmarkReportPath;
    if (reportPath.empty()) {
        return true;
    }

    nlohmann::json root;
    if (m_validationRun.enabled()) {
        const app::validation::ValidationSceneContract& contract = m_validationRun.sceneContract();
        root["kind"] = "mecraft.validation_capture_report";
        root["version"] = 1;
        root["scene"] = validationSceneStableId(m_validationRun.scene());
        root["scene_contract"] = {{"id", contract.id},
                                  {"version", contract.version},
                                  {"content_hash", renderer::contracts::stableContentHashHex(contract.contentHash)},
                                  {"hash_algorithm", renderer::contracts::kStableContentHashAlgorithm},
                                  {"source", contract.sourcePath.generic_u8string()}};
        root["camera_path"] = {
            {"id", m_validationRun.cameraPath().id},
            {"content_hash", renderer::contracts::cameraPathContentHashHex(m_validationRun.cameraPath().contentHash)},
            {"duration_seconds", m_validationRun.cameraPath().durationSeconds},
            {"source", contract.cameraPath.source.generic_u8string()}};
        root["capture"] = {{"path", m_launchOptions.validationCapturePath.generic_u8string()},
                           {"width", m_launchOptions.validationWidth},
                           {"height", m_launchOptions.validationHeight},
                           {"camera_time_seconds", m_validationRun.cameraPath().durationSeconds}};
        root["warmup_frame_count"] = m_launchOptions.validationWarmupFrames;
        root["requested_sample_frame_count"] = m_launchOptions.validationSampleFrames;
        const app::validation::ValidationRenderSettingsProfile& profile = m_validationRun.renderSettingsProfile();
        root["render_settings"] = {{"id", profile.id},
                                   {"version", profile.version},
                                   {"content_hash", renderer::contracts::stableContentHashHex(profile.contentHash)}};
        root["environment"] = {{"time_of_day_seconds", contract.environment.timeOfDaySeconds},
                               {"weather", app::validation::validationWeatherStableId(contract.environment.weather)}};
        if (m_validationRun.scene() == ValidationScene::Voxel) {
            const app::validation::ValidationVoxelWorldIdentity& world = *contract.voxelWorld;
            root["voxel_world"] = {{"generator", {{"id", world.generatorId}, {"version", world.generatorVersion}}},
                                   {"seed", world.seed},
                                   {"render_distance", world.renderDistance},
                                   {"content_hash", renderer::contracts::stableContentHashHex(world.contentHash)},
                                   {"persistent_writes_enabled", false}};
            if (world.fixture.has_value()) {
                root["voxel_world"]["fixture"] = {
                    {"id", world.fixture->id},
                    {"version", world.fixture->version},
                    {"content_hash", renderer::contracts::stableContentHashHex(world.fixture->contentHash)}};
            }
        } else {
            const app::validation::ValidationModelAssetIdentity& asset = *contract.modelAsset;
            root["model_asset"] = {{"source", asset.source.generic_u8string()},
                                   {"content_hash", renderer::contracts::stableContentHashHex(asset.contentHash)}};
            if (contract.modelProbeGrid.has_value()) {
                root["reflection_probe_grid"] = {
                    {"spacing_meters", contract.modelProbeGrid->spacingMeters},
                    {"bounds_padding_meters", contract.modelProbeGrid->boundsPaddingMeters}};
            }
        }
    } else {
        root["kind"] = "mecraft.benchmark_frame_report";
        root["world"] = m_launchOptions.benchmarkWorldName;
        root["replay_input"] = m_launchOptions.inputReplayPath.string();
        root["benchmark_duration_seconds"] = m_launchOptions.benchmarkDurationSeconds;
    }
    root["rhi_backend"] = renderer::rhi::rhiBackendConfigName(m_launchOptions.rhiBackend);
    root["frame_count"] = m_benchmarkStats.frameCount;
    nlohmann::json framePercentiles = {{"average", avgFrameMs},
                                       {"median", p50FrameMs},
                                       {"p50", p50FrameMs},
                                       {"p95", p95FrameMs},
                                       {"p99", p99FrameMs},
                                       {"min", m_benchmarkStats.minFrameMs},
                                       {"max", m_benchmarkStats.maxFrameMs}};
    if (m_validationRun.enabled()) {
        root["sample_duration_seconds"] = m_benchmarkStats.replayActiveSeconds;
        root["cpu_update_render_ms"] = std::move(framePercentiles);
    } else {
        root["replay_active_seconds"] = m_benchmarkStats.replayActiveSeconds;
        root["frame_ms"] = std::move(framePercentiles);
        root["fps"] = {{"average", avgFps}};
    }
    nlohmann::json gpuStages = nlohmann::json::object();
    for (const GpuTimerPassWindowStats& stage : gpuTimingWindow.passes) {
        gpuStages[gpuTimerPassName(stage.pass)] = gpuTimingPercentilesJson(stage.gpuMs);
    }
    root["render_graph_stage_ms"] = {{"valid", gpuTimingWindow.valid},
                                     {"window_capacity", gpuTimingWindow.capacity},
                                     {"window_sample_count", gpuTimingWindow.sampleCount},
                                     {"observed_sample_count", gpuTimingWindow.observedSampleCount},
                                     {"total_tracked", gpuTimingPercentilesJson(gpuTimingWindow.totalTrackedGpuMs)},
                                     {"stages", std::move(gpuStages)}};
    root["rhi_memory"] = rhiMemoryStatsJson(memoryStats);

    const std::filesystem::path parentPath = reportPath.parent_path();
    if (!parentPath.empty()) {
        std::error_code createError;
        std::filesystem::create_directories(parentPath, createError);
        if (createError) {
            std::cerr << "[Benchmark] Failed to create report directory: " << parentPath << ": "
                      << createError.message() << '\n';
            m_benchmarkReportSucceeded = false;
            return false;
        }
    }

    std::ofstream output(reportPath);
    if (!output) {
        std::cerr << "[Benchmark] Failed to write report: " << reportPath << '\n';
        m_benchmarkReportSucceeded = false;
        return false;
    }
    output << root.dump(2);
    output.flush();
    if (!output) {
        std::cerr << "[Benchmark] Failed to flush report: " << reportPath << '\n';
        m_benchmarkReportSucceeded = false;
        return false;
    }
    std::cout << "[Benchmark] Report written to " << reportPath << '\n';
    return true;
}

void GameManager::shutdown() {
    if (!writeBenchmarkReport()) {
        m_runSucceeded = false;
    }
    if (m_rhiDevice) {
        // Application states own render resources referenced by the latest
        // submissions, so all GPU work must finish before those states are
        // destroyed.
        m_rhiDevice->waitIdle();
    }
    while (!m_appStateMachine.isEmpty()) {
        m_appStateMachine.popState();
    }
    m_uiRenderer.shutdown();
    m_resourceMgr.shutdown();
    m_bgmSystem.shutdown();
    m_audioEngine.shutdown();
    if (m_rhiDevice) {
        m_commandListPool.reset();
        m_rhiDevice->shutdown();
        m_rhiDevice.reset();
    }
#if defined(MECRAFT_ENABLE_STREAMLINE)
    {
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        if (streamline.initialized() && !streamline.shutdown()) {
            MECRAFT_LOG_STREAM(std::cerr << streamline.lastError() << '\n');
        }
    }
#endif
    net::ENetTransport::deinitialize();
    m_threadPool.shutdown();
    m_input.shutdownInputReplay();
    m_window.destroy();
}
