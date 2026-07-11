#include "GameplayRenderRuntime.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/renderers/BlockEntityRenderer.h"
#include "../../renderer/renderers/DropRenderer.h"
#include "../../renderer/renderers/FallingBlockRenderer.h"
#include "../../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../../renderer/renderers/HumanoidRenderer.h"
#include "../session/GameSession.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ecs/GameplayScene.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../particle/RainRenderer.h"
#include "../../app/AppSettings.h"

#include <algorithm>
#include <cstddef>

#ifdef MECRAFT_DEBUG
#include "../../engine/platform/Window.h"
#include "../debug/DebugFrameProfiler.h"
#endif

#include <glad/glad.h>

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
    RainRenderer* rainRenderer = nullptr;

#ifdef MECRAFT_DEBUG
    Dashboard dashboard;
    DebugFrameProfiler profiler;
    Dashboard::FrameProfilerStats dashboardProfilerStats;
#endif
};

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
                                  RhiDevice& rhiDevice) {
    auto& renderer = m_impl->resourceHub;
    auto& renderScene = m_impl->scene;
    auto& blockEntityRenderer = m_impl->blockEntityRenderer;
    auto& dropRenderer = m_impl->dropRenderer;
    auto& fallingBlockRenderer = m_impl->fallingBlockRenderer;
    auto& firstPersonHeldItemRenderer = m_impl->firstPersonHeldItemRenderer;
    auto& humanoidRenderer = m_impl->humanoidRenderer;

    // Core GPU infrastructure
    if (!renderer.init(resourceMgr, threadPool, rhiDevice)) {
        return false;
    }

    // Initialize RenderScene and connect to RenderResourceHub
    renderScene.init(resourceMgr);
    const RenderSettings initialSettings = app::loadRenderSettings(renderer.getSettings());
    renderScene.setupResources(
        &threadPool,
        &renderer.rhiDevice(),
        &renderer.getTerrainRenderer(),
        &renderer.getTerrainRhiPipelineSet(),
        &renderer.getWorldRenderBuffer(),
        &renderer.getDeferredRenderTargets(),
        &renderer.getGameplaySkyRenderer(),
        &renderer.getShadowRenderer(),
        initialSettings
    );
    renderScene.setSettingsChangedCallback([](const RenderSettings& settings) {
        app::saveRenderSettings(settings);
    });

    // Inject RenderScene services into RenderResourceHub
    renderer.setTerrainStreamingService(&renderScene.getTerrainStreamingService());
    renderer.setOverlayRenderer(&renderScene.getOverlayRenderer());
    renderer.setDebugService(&renderScene.debugService());

    // Entity renderers
    blockEntityRenderer.init(resourceMgr);
    dropRenderer.init(resourceMgr);
    fallingBlockRenderer.init(resourceMgr);
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
    session.particleSystem().init(resourceMgr);
    m_impl->rainRenderer = &session.rainRenderer();
    if (!m_impl->rainRenderer->init(resourceMgr)) {
        m_impl->rainRenderer = nullptr;
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    return true;
}

void GameplayRenderRuntime::shutdown() {
    // Reverse order of initialization
#ifdef MECRAFT_DEBUG
    m_impl->dashboard.shutdown();
#endif
    if (m_impl->rainRenderer != nullptr) {
        m_impl->rainRenderer->shutdown();
        m_impl->rainRenderer = nullptr;
    }
    m_impl->humanoidRenderer.shutdown();
    m_impl->firstPersonHeldItemRenderer.shutdown();
    m_impl->fallingBlockRenderer.shutdown();
    m_impl->dropRenderer.shutdown();
    m_impl->blockEntityRenderer.shutdown();
    m_impl->scene.shutdown();
    m_impl->resourceHub.shutdown();
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

#ifdef MECRAFT_DEBUG
void GameplayRenderRuntime::initDebug(Window& window) {
    m_impl->dashboard.init(window);
    m_impl->dashboard.setFirstPersonHeldItemRenderer(&m_impl->firstPersonHeldItemRenderer);
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
