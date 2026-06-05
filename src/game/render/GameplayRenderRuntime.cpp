#include "GameplayRenderRuntime.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/renderers/DropRenderer.h"
#include "../../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../../renderer/renderers/HumanoidRenderer.h"
#include "../session/GameSession.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ecs/GameplayScene.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../particle/RainRenderer.h"

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
    DropRenderer dropRenderer;
    FirstPersonHeldItemRenderer firstPersonHeldItemRenderer;
    HumanoidRenderer humanoidRenderer;

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

void GameplayRenderRuntime::init(ResourceMgr& resourceMgr,
                                  GameSession& session,
                                  UIRenderer& uiRenderer,
                                  ThreadPool& threadPool) {
    auto& renderer = m_impl->resourceHub;
    auto& renderScene = m_impl->scene;
    auto& dropRenderer = m_impl->dropRenderer;
    auto& firstPersonHeldItemRenderer = m_impl->firstPersonHeldItemRenderer;
    auto& humanoidRenderer = m_impl->humanoidRenderer;

    // Core GPU infrastructure
    renderer.init(resourceMgr, threadPool);

    // Initialize RenderScene and connect to RenderResourceHub
    renderScene.init(resourceMgr);
    renderScene.setupResources(
        &threadPool,
        &renderer.getTerrainRenderer(),
        &renderer.getWorldRenderBuffer(),
        &renderer.getDeferredRenderTargets(),
        &renderer.getGameplaySkyRenderer(),
        &renderer.getShadowRenderer(),
        renderer.getSettings()
    );

    // Inject RenderScene services into RenderResourceHub
    renderer.setTerrainStreamingService(&renderScene.getTerrainStreamingService());
    renderer.setOverlayRenderer(&renderScene.getOverlayRenderer());
    renderer.setDebugService(&renderScene.debugService());

    // Enable fog via RenderSettings
    RenderSettings settings = renderScene.getSettings();
    settings.fog.enabled = true;
    renderScene.setSettings(settings);

    // Entity renderers
    dropRenderer.init(resourceMgr);
    firstPersonHeldItemRenderer.init(resourceMgr);
    humanoidRenderer.init(resourceMgr);

    // Cross-wire renderers into RenderScene
    renderScene.setHumanoidRenderer(&humanoidRenderer);
    renderScene.setDropRenderer(&dropRenderer);
    renderScene.setDropSystem(&session.dropSystem());
    renderScene.setGameplayRegistry(&session.gameplayScene().registry());
    renderScene.setParticleSystem(&session.particleSystem());

    // UI needs humanoid renderer for inventory preview
    uiRenderer.setHumanoidRenderer(&humanoidRenderer);

    // Particle and rain systems (owned by session, init requires ResourceMgr)
    session.particleSystem().init(resourceMgr);
    session.rainRenderer().init(resourceMgr);

    glEnable(GL_DEPTH_TEST);
}

void GameplayRenderRuntime::shutdown() {
    // Reverse order of initialization
#ifdef MECRAFT_DEBUG
    m_impl->dashboard.shutdown();
#endif
    m_impl->humanoidRenderer.shutdown();
    m_impl->firstPersonHeldItemRenderer.shutdown();
    m_impl->dropRenderer.shutdown();
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
    stats.fixedUpdateMs = timing.fixedUpdateMs;
    stats.fixedInputMs = timing.fixedInputMs;
    stats.fixedStateUpdateMs = timing.fixedStateUpdateMs;
    stats.fixedParticleUpdateMs = timing.fixedParticleUpdateMs;
    stats.fixedDropUpdateMs = timing.fixedDropUpdateMs;
    stats.fixedWorldUpdateMs = timing.fixedWorldUpdateMs;
    stats.audioMs = timing.audioMs;
    stats.renderMs = timing.renderMs;
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
