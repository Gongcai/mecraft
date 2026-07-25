#include "GameplayRenderRuntime.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/renderers/BlockEntityRenderer.h"
#include "../../renderer/renderers/DropRenderer.h"
#include "../../renderer/renderers/FallingBlockRenderer.h"
#include "../../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../../renderer/renderers/HumanoidRenderer.h"
#include "../../renderer/presentation/PresentationController.h"
#if defined(MECRAFT_ENABLE_STREAMLINE)
#include "../../renderer/upscaling/DlssFrameGeneration.h"
#include "../../renderer/upscaling/StreamlineRuntime.h"
#endif
#include "../session/GameSession.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ecs/GameplayScene.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../particle/RainRenderer.h"
#include "../../app/AppSettings.h"
#include "../../engine/platform/Window.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>

#ifdef MECRAFT_DEBUG
#include "../../engine/platform/Window.h"
#include "../debug/DebugFrameProfiler.h"
#endif


// --------------------------------------------------------------------------
// PIMPL definition — mirrors the old Game::RenderRuntime struct.
// All members are default-constructed; actual initialization in init().
// --------------------------------------------------------------------------
struct GameplayRenderRuntime::Impl {
    RenderResourceHub resourceHub;
    RenderScene scene;
    BlockEntityRenderer blockEntityRenderer;
    DropRenderer dropRenderer;
    FallingBlockRenderer fallingBlockRenderer;
    FirstPersonHeldItemRenderer firstPersonHeldItemRenderer;
    HumanoidRenderer humanoidRenderer;
    std::unique_ptr<PresentationBackend> presentationBackend;
    std::unique_ptr<PresentationController> presentationController;
    RainRenderer* rainRenderer = nullptr;
    ParticleSystem* particleSystem = nullptr;
#if defined(MECRAFT_ENABLE_STREAMLINE)
    std::optional<StreamlineReflexMode> pendingReflexMode;
#endif

#ifdef MECRAFT_DEBUG
    Dashboard dashboard;
    DebugFrameProfiler profiler;
    Dashboard::FrameProfilerStats dashboardProfilerStats;
#endif
};

namespace {

#if defined(MECRAFT_ENABLE_STREAMLINE)
[[nodiscard]] StreamlineReflexMode toStreamlineReflexMode(
    const ReflexLowLatencyMode mode) {
    switch (mode) {
        case ReflexLowLatencyMode::Off:
            return StreamlineReflexMode::Off;
        case ReflexLowLatencyMode::On:
            return StreamlineReflexMode::LowLatency;
        case ReflexLowLatencyMode::OnWithBoost:
            return StreamlineReflexMode::LowLatencyWithBoost;
    }
    return StreamlineReflexMode::Off;
}

[[nodiscard]] bool applyNvidiaFeatureSettings(
    const RenderSettings& settings,
    const RenderScene& renderScene,
    PresentationController& presentation,
    std::optional<StreamlineReflexMode>& pendingReflexMode) {
    const bool frameGenerationEnabled =
        settings.nvidia.frameGeneration == FrameGenerationType::Dlss;
    if (frameGenerationEnabled &&
        settings.nvidia.reflexMode == ReflexLowLatencyMode::Off) {
        std::cerr << "GameplayRenderRuntime: DLSS Frame Generation requires NVIDIA Reflex\n";
        return false;
    }
    if (frameGenerationEnabled &&
        !supportsDlssFrameGenerationInputs(
            settings, renderScene.isFsr1Supported())) {
        std::cerr << "GameplayRenderRuntime: the selected renderer cannot produce DLSS Frame Generation inputs\n";
        return false;
    }

    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    const StreamlineReflexMode reflexMode =
        toStreamlineReflexMode(settings.nvidia.reflexMode);
    const bool frameGenerationWasEnabled =
        presentation.frameGenerationSwapchainEnabled();
    if (frameGenerationEnabled) {
        if (!presentation.frameGenerationAvailable()) {
            std::cerr << "GameplayRenderRuntime: DLSS Frame Generation is unavailable\n";
            return false;
        }
        if (!streamline.configureReflex(reflexMode)) {
            std::cerr << streamline.lastError() << '\n';
            return false;
        }
        pendingReflexMode.reset();
        return presentation.requestFrameGenerationEnabled(true);
    }

    if (presentation.frameGenerationAvailable() &&
        !presentation.requestFrameGenerationEnabled(false)) {
        std::cerr << "GameplayRenderRuntime: the frame-generation disable request was rejected\n";
        return false;
    }
    if (reflexMode == StreamlineReflexMode::Off &&
        frameGenerationWasEnabled) {
        pendingReflexMode = reflexMode;
        return true;
    }
    if (!streamline.configureReflex(reflexMode)) {
        std::cerr << streamline.lastError() << '\n';
        return false;
    }
    pendingReflexMode.reset();
    return true;
}
#endif

} // namespace

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------

GameplayRenderRuntime::GameplayRenderRuntime()
    : m_impl(std::make_unique<Impl>()) {
}

GameplayRenderRuntime::~GameplayRenderRuntime() = default;

bool GameplayRenderRuntime::init(ResourceMgr& resourceMgr,
                                  GameSession& session,
                                  UIRenderer& uiRenderer,
                                  ThreadPool& threadPool,
                                  Window& window,
                                  RhiDevice& rhiDevice,
                                  RhiCommandListPool& commandListPool) {
    auto& renderer = m_impl->resourceHub;
    auto& renderScene = m_impl->scene;
    auto& blockEntityRenderer = m_impl->blockEntityRenderer;
    auto& dropRenderer = m_impl->dropRenderer;
    auto& fallingBlockRenderer = m_impl->fallingBlockRenderer;
    auto& firstPersonHeldItemRenderer = m_impl->firstPersonHeldItemRenderer;
    auto& humanoidRenderer = m_impl->humanoidRenderer;

#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (rhiDevice.backend() == RhiBackend::Vulkan) {
        m_impl->presentationBackend =
            createDlssFrameGenerationPresentationBackend(rhiDevice);
    } else {
        m_impl->presentationBackend = createNativePresentationBackend(rhiDevice);
    }
#else
    m_impl->presentationBackend = createNativePresentationBackend(rhiDevice);
#endif
    if (m_impl->presentationBackend == nullptr) {
        return false;
    }
    m_impl->presentationController = std::make_unique<PresentationController>(
        *m_impl->presentationBackend);
    if (!m_impl->presentationController->initUiComposition(rhiDevice)) {
        return false;
    }
    if (!m_impl->presentationController->initWindowState(window)) {
        return false;
    }

    // Core GPU infrastructure
    if (!renderer.init(resourceMgr, threadPool, rhiDevice, commandListPool)) {
        return false;
    }

    // Initialize RenderScene and connect to RenderResourceHub
    renderScene.init(resourceMgr);
    const RenderSettings initialSettings = app::loadRenderSettings(renderer.getSettings());
    renderScene.setupResources(
        &threadPool,
        &renderer.rhiDevice(),
        &renderer.commandListPool(),
        &renderer.getTerrainRenderer(),
        &renderer.getTerrainRhiPipelineSet(),
        &renderer.getWorldRenderBuffer(),
        &renderer.getDeferredRenderTargets(),
        &renderer.getGameplaySkyRenderer(),
        &renderer.getShadowRenderer(),
        initialSettings
    );
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (rhiDevice.backend() == RhiBackend::Vulkan &&
        !applyNvidiaFeatureSettings(
            initialSettings, renderScene,
            *m_impl->presentationController,
            m_impl->pendingReflexMode)) {
        return false;
    }
#endif
    renderScene.setSettingsChangedCallback([this](const RenderSettings& settings) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (m_impl->resourceHub.rhiDevice().backend() == RhiBackend::Vulkan &&
            !applyNvidiaFeatureSettings(
                settings, m_impl->scene,
                *m_impl->presentationController,
                m_impl->pendingReflexMode)) {
            return false;
        }
#endif
        app::saveRenderSettings(settings);
        return true;
    });

    // Inject RenderScene services into RenderResourceHub
    renderer.setTerrainStreamingService(&renderScene.getTerrainStreamingService());
    renderer.setOverlayRenderer(&renderScene.getOverlayRenderer());
    renderer.setDebugService(&renderScene.debugService());

    // Entity renderers
    blockEntityRenderer.init(resourceMgr);
    dropRenderer.init(resourceMgr);
    if (!fallingBlockRenderer.init(resourceMgr)) {
        return false;
    }
    firstPersonHeldItemRenderer.init(resourceMgr, renderer.rhiDevice());
    humanoidRenderer.init(resourceMgr, renderer.rhiDevice());

    // Cross-wire renderers into RenderScene
    renderScene.setBlockEntityRenderer(&blockEntityRenderer);
    renderScene.setHumanoidRenderer(&humanoidRenderer);
    renderScene.setDropRenderer(&dropRenderer);
    renderScene.setFallingBlockRenderer(&fallingBlockRenderer);
    renderScene.setDropSystem(&session.dropSystem());
    renderScene.setGameplayRegistry(&session.gameplayScene().registry());
    renderScene.setParticleSystem(&session.particleSystem());

    // UI needs humanoid renderer for inventory preview
    uiRenderer.setHumanoidRenderer(&humanoidRenderer);

    // Particle and rain systems (owned by session, init requires ResourceMgr)
    m_impl->particleSystem = &session.particleSystem();
    if (!m_impl->particleSystem->init(resourceMgr)) {
        m_impl->particleSystem = nullptr;
        return false;
    }
    m_impl->rainRenderer = &session.rainRenderer();
    if (!m_impl->rainRenderer->init(resourceMgr)) {
        m_impl->rainRenderer = nullptr;
        return false;
    }

    return true;
}

void GameplayRenderRuntime::shutdown() {
    // Reverse order of initialization
#ifdef MECRAFT_DEBUG
    m_impl->dashboard.shutdown();
#endif
    if (m_impl->presentationBackend != nullptr &&
        m_impl->presentationBackend->frameGenerationEnabled() &&
        !m_impl->presentationBackend->setFrameGenerationEnabled(false)) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        std::cerr << StreamlineRuntime::instance().lastError() << '\n';
#else
        std::cerr << "GameplayRenderRuntime: failed to disable frame generation during shutdown\n";
#endif
    }
    if (m_impl->presentationController != nullptr) {
        m_impl->presentationController->shutdownUiComposition();
    }
    if (m_impl->rainRenderer != nullptr) {
        m_impl->rainRenderer->shutdown();
        m_impl->rainRenderer = nullptr;
    }
    if (m_impl->particleSystem != nullptr) {
        m_impl->particleSystem->shutdown();
        m_impl->particleSystem = nullptr;
    }
    m_impl->humanoidRenderer.shutdown();
    m_impl->firstPersonHeldItemRenderer.shutdown();
    m_impl->fallingBlockRenderer.shutdown();
    m_impl->dropRenderer.shutdown();
    m_impl->blockEntityRenderer.shutdown();
    m_impl->scene.shutdown();
    m_impl->resourceHub.shutdown();
    m_impl->presentationController.reset();
    m_impl->presentationBackend.reset();
}

// --------------------------------------------------------------------------
// Accessors
// --------------------------------------------------------------------------

RenderResourceHub& GameplayRenderRuntime::resourceHub() {
    return m_impl->resourceHub;
}

RenderScene& GameplayRenderRuntime::renderScene() {
    return m_impl->scene;
}

FirstPersonHeldItemRenderer& GameplayRenderRuntime::firstPersonHeldItemRenderer() {
    return m_impl->firstPersonHeldItemRenderer;
}

PresentationController& GameplayRenderRuntime::presentationController() {
    return *m_impl->presentationController;
}

bool GameplayRenderRuntime::applyFrameBoundaryNvidiaSettings() {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (!m_impl->pendingReflexMode.has_value()) {
        return true;
    }
    if (m_impl->presentationController == nullptr ||
        m_impl->presentationController->frameGenerationSwapchainEnabled()) {
        std::cerr << "GameplayRenderRuntime: NVIDIA Reflex cannot be disabled while DLSS Frame Generation is enabled\n";
        return false;
    }
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (!streamline.configureReflex(*m_impl->pendingReflexMode)) {
        std::cerr << streamline.lastError() << '\n';
        return false;
    }
    m_impl->pendingReflexMode.reset();
#endif
    return true;
}

#ifdef MECRAFT_DEBUG
bool GameplayRenderRuntime::initDebug(Window& window, RhiDevice& rhiDevice) {
    if (!m_impl->dashboard.init(window, rhiDevice)) {
        return false;
    }
    m_impl->dashboard.setFirstPersonHeldItemRenderer(&m_impl->firstPersonHeldItemRenderer);
    return true;
}

void GameplayRenderRuntime::publishDebugStats(const double frameTime) {
    m_impl->profiler.publish(frameTime);

    const auto& timing = m_impl->profiler.timing();
    const auto& history = m_impl->profiler.history();

    auto& stats = m_impl->dashboardProfilerStats;
    stats.frameMs = frameTime * 1000.0;
    stats.fixedUpdateMs = timing.currentFixedUpdateMs;
    stats.fixedInputMs = timing.currentFixedInputMs;
    stats.fixedStateUpdateMs = timing.currentFixedStateUpdateMs;
    stats.fixedParticleUpdateMs = timing.currentFixedParticleUpdateMs;
    stats.fixedDropUpdateMs = timing.currentFixedDropUpdateMs;
    stats.fixedWorldUpdateMs = timing.currentFixedWorldUpdateMs;
    stats.audioMs = timing.currentAudioMs;
    stats.renderMs = timing.currentRenderMs;
    stats.pollEventsMs = timing.currentPollEventsMs;
    stats.appUpdateDispatchMs = timing.currentAppUpdateDispatchMs;
    stats.appRenderDispatchMs = timing.currentAppRenderDispatchMs;
    stats.renderSnapshotMs = timing.currentRenderSnapshotMs;
    stats.renderSceneMs = timing.currentRenderSceneMs;
    stats.renderUiMs = timing.currentRenderUiMs;
    stats.renderDashboardMs = timing.currentRenderDashboardMs;
    stats.swapBuffersMs = timing.currentSwapBuffersMs;
    stats.renderOtherMs = std::max(0.0, stats.renderMs - stats.renderSnapshotMs - stats.renderSceneMs -
                                            stats.renderUiMs - stats.renderDashboardMs - stats.swapBuffersMs);
    stats.untrackedMs = std::max(0.0, stats.frameMs - stats.pollEventsMs -
                                          stats.appUpdateDispatchMs - stats.appRenderDispatchMs);
    stats.pollInputCallbackMs = timing.currentPollInputCallbackMs;
    stats.pollCursorPosCallbackMs = timing.currentPollCursorPosCallbackMs;
    stats.pollImguiCallbackMs = timing.currentPollImguiCallbackMs;
    stats.pollImguiCursorPosCallbackMs = timing.currentPollImguiCursorPosCallbackMs;
    stats.pollImguiCursorPosBackendMs = timing.currentPollImguiCursorPosBackendMs;
    stats.pollImguiWndProcMs = timing.currentPollImguiWndProcMs;
    stats.pollImguiWndProcSlowestMs = timing.currentPollImguiWndProcSlowestMs;
    stats.pollImguiWndProcSlowestMsg = timing.currentPollImguiWndProcSlowestMsg;
    stats.pollImguiWndProcCount = timing.currentPollImguiWndProcCount;
    stats.pollEventCount = timing.currentPollEventCounts.total();
    stats.pollKeyEventCount = timing.currentPollEventCounts.keyEvents;
    stats.pollMouseButtonEventCount = timing.currentPollEventCounts.mouseButtonEvents;
    stats.pollCursorPosEventCount = timing.currentPollEventCounts.cursorPosEvents;
    stats.pollScrollEventCount = timing.currentPollEventCounts.scrollEvents;
    stats.pollCharEventCount = timing.currentPollEventCounts.charEvents;

    const PresentationStatistics& presentation =
        m_impl->presentationController->statistics();
    stats.presentationMode = presentation.mode;
    stats.realFramesAcquired = presentation.realFramesAcquired;
    stats.realFramesPresented = presentation.realFramesPresented;
    stats.generatedFramesPresented = presentation.generatedFramesPresented;
    stats.displayedFrames = presentation.displayedFrames;
    stats.presentationSkippedFrames = presentation.skippedFrames;
    stats.presentationFailedOperations = presentation.failedOperations;
    stats.presentationVsyncChanges = presentation.vsyncChanges;
    stats.presentationFullscreenChanges = presentation.fullscreenChanges;
    stats.presentationVsyncEnabled = presentation.vsyncEnabled;
    stats.presentationFullscreenEnabled = presentation.fullscreenEnabled;

    // Snapshot max-frame-time: when current frame is the worst, record all timings
    if (stats.frameMs > stats.maxFrameMs) {
        stats.maxFrameMs = stats.frameMs;
        stats.maxFixedUpdateMs = stats.fixedUpdateMs;
        stats.maxFixedInputMs = stats.fixedInputMs;
        stats.maxFixedStateUpdateMs = stats.fixedStateUpdateMs;
        stats.maxFixedParticleUpdateMs = stats.fixedParticleUpdateMs;
        stats.maxFixedDropUpdateMs = stats.fixedDropUpdateMs;
        stats.maxFixedWorldUpdateMs = stats.fixedWorldUpdateMs;
        stats.maxAudioMs = stats.audioMs;
        stats.maxRenderMs = stats.renderMs;
        stats.maxPollEventsMs = stats.pollEventsMs;
        stats.maxAppUpdateDispatchMs = stats.appUpdateDispatchMs;
        stats.maxAppRenderDispatchMs = stats.appRenderDispatchMs;
        stats.maxRenderSnapshotMs = stats.renderSnapshotMs;
        stats.maxRenderSceneMs = stats.renderSceneMs;
        stats.maxRenderUiMs = stats.renderUiMs;
        stats.maxRenderDashboardMs = stats.renderDashboardMs;
        stats.maxSwapBuffersMs = stats.swapBuffersMs;
        stats.maxRenderOtherMs = stats.renderOtherMs;
        stats.maxUntrackedMs = stats.untrackedMs;
        stats.maxPollInputCallbackMs = stats.pollInputCallbackMs;
        stats.maxPollCursorPosCallbackMs = stats.pollCursorPosCallbackMs;
        stats.maxPollImguiCallbackMs = stats.pollImguiCallbackMs;
        stats.maxPollImguiCursorPosCallbackMs = stats.pollImguiCursorPosCallbackMs;
        stats.maxPollImguiCursorPosBackendMs = stats.pollImguiCursorPosBackendMs;
        stats.maxPollImguiWndProcMs = stats.pollImguiWndProcMs;
        stats.maxPollImguiWndProcSlowestMs = stats.pollImguiWndProcSlowestMs;
        stats.maxPollImguiWndProcSlowestMsg = stats.pollImguiWndProcSlowestMsg;
        stats.maxPollImguiWndProcCount = stats.pollImguiWndProcCount;
        stats.maxPollEventCount = stats.pollEventCount;
        stats.maxPollKeyEventCount = stats.pollKeyEventCount;
        stats.maxPollMouseButtonEventCount = stats.pollMouseButtonEventCount;
        stats.maxPollCursorPosEventCount = stats.pollCursorPosEventCount;
        stats.maxPollScrollEventCount = stats.pollScrollEventCount;
        stats.maxPollCharEventCount = stats.pollCharEventCount;
    }
    stats.frameHistoryCount = history.frameCount;
    stats.fixedHistoryCount = history.count;

    // Copy history ring buffer entries in chronological order for the dashboard
    const size_t srcSize = DebugFrameProfiler::kHistorySamples;
    auto copyHistory = [srcSize](const float* src, float* dst, size_t dstSize, size_t count, size_t writeIndex) {
        std::fill(dst, dst + dstSize, 0.0f);
        for (size_t i = 0; i < count && i < dstSize; ++i) {
            dst[i] = src[(writeIndex + srcSize - count + i) % srcSize];
        }
    };
    copyHistory(history.fpsHistory.data(), stats.fpsHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.frameCount, history.frameWriteIndex);
    copyHistory(history.renderHistory.data(), stats.renderHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.count, history.writeIndex);
    copyHistory(history.fixedUpdateHistory.data(), stats.fixedUpdateHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.count, history.writeIndex);
    copyHistory(history.fixedInputHistory.data(), stats.fixedInputHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.count, history.writeIndex);
    copyHistory(history.fixedStateHistory.data(), stats.fixedStateHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.count, history.writeIndex);
    copyHistory(history.fixedParticleHistory.data(), stats.fixedParticleHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.count, history.writeIndex);
    copyHistory(history.fixedDropHistory.data(), stats.fixedDropHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.count, history.writeIndex);
    copyHistory(history.fixedWorldHistory.data(), stats.fixedWorldHistory.data(), Dashboard::FrameProfilerStats::kFixedHistorySamples,
                history.count, history.writeIndex);
}

Dashboard* GameplayRenderRuntime::dashboard() {
    return &m_impl->dashboard;
}

DebugFrameProfiler* GameplayRenderRuntime::profiler() {
    return &m_impl->profiler;
}

Dashboard::FrameProfilerStats* GameplayRenderRuntime::dashboardProfilerStats() {
    return &m_impl->dashboardProfilerStats;
}
#endif
