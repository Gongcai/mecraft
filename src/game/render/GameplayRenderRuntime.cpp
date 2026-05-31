#include "GameplayRenderRuntime.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/renderers/DropRenderer.h"
#include "../../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../../renderer/renderers/HumanoidRenderer.h"
#include "../../renderer/renderers/PostProcessRenderer.h"
#include "../session/GameSession.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ecs/GameplayScene.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../particle/RainRenderer.h"

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
    PostProcessRenderer postProcessRenderer;
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
                                  UIRenderer& uiRenderer) {
    auto& renderer = m_impl->resourceHub;
    auto& renderScene = m_impl->scene;
    auto& dropRenderer = m_impl->dropRenderer;
    auto& firstPersonHeldItemRenderer = m_impl->firstPersonHeldItemRenderer;
    auto& humanoidRenderer = m_impl->humanoidRenderer;
    auto& postProcessRenderer = m_impl->postProcessRenderer;

    // Core GPU infrastructure
    renderer.init(resourceMgr);

    // Initialize RenderScene and connect to RenderResourceHub
    renderScene.init(resourceMgr);
    renderScene.initFromRenderer(&renderer);

    // Enable fog via RenderSettings
    RenderSettings settings = renderScene.getSettings();
    settings.fog.enabled = true;
    renderScene.setSettings(settings);

    // Entity renderers
    dropRenderer.init(resourceMgr);
    firstPersonHeldItemRenderer.init(resourceMgr);
    humanoidRenderer.init(resourceMgr);

    // Cross-wire renderers into RenderResourceHub
    renderer.setHumanoidRenderer(&humanoidRenderer);
    renderer.setDropRenderer(&dropRenderer);
    renderer.setDropSystem(&session.dropSystem());
    renderer.setGameplayRegistry(&session.gameplayScene().registry());
    renderer.setParticleSystem(&session.particleSystem());

    // Cross-wire renderers into RenderScene
    renderScene.setHumanoidRenderer(&humanoidRenderer);
    renderScene.setDropRenderer(&dropRenderer);
    renderScene.setDropSystem(&session.dropSystem());
    renderScene.setGameplayRegistry(&session.gameplayScene().registry());
    renderScene.setParticleSystem(&session.particleSystem());

    // UI needs humanoid renderer for inventory preview
    uiRenderer.setHumanoidRenderer(&humanoidRenderer);

    // Post-processing
    postProcessRenderer.init(resourceMgr);

    // Particle and rain systems (owned by session, init requires ResourceMgr)
    session.particleSystem().init(resourceMgr);
    session.rainRenderer().init(resourceMgr);

    glEnable(GL_DEPTH_TEST);
}

void GameplayRenderRuntime::shutdown() {
    // Reverse order of initialization
    m_impl->postProcessRenderer.shutdown();
    m_impl->humanoidRenderer.shutdown();
    m_impl->firstPersonHeldItemRenderer.shutdown();
    m_impl->dropRenderer.shutdown();
    m_impl->resourceHub.shutdown();
}

// --------------------------------------------------------------------------
// Accessors
// --------------------------------------------------------------------------

ThreadPool* GameplayRenderRuntime::getThreadPool() {
    return m_impl->resourceHub.getThreadPool();
}

RenderResourceHub& GameplayRenderRuntime::resourceHub() {
    return m_impl->resourceHub;
}

RenderScene& GameplayRenderRuntime::renderScene() {
    return m_impl->scene;
}

FirstPersonHeldItemRenderer& GameplayRenderRuntime::firstPersonHeldItemRenderer() {
    return m_impl->firstPersonHeldItemRenderer;
}

PostProcessRenderer& GameplayRenderRuntime::postProcessRenderer() {
    return m_impl->postProcessRenderer;
}
