#include "GameplayHudPresenter.h"
#include "../../ui/core/UIRenderer.h"
#include "../../engine/platform/Window.h"
#include "../states/GameStateMachine.h"
#include "../../engine/input/InputManager.h"

#include <cstdlib>

namespace {
PlayerStatsData toPlayerStatsData(const GameplayPresentationSnapshot& snap) {
    PlayerStatsData playerStats;
    playerStats.health = snap.playerStats.health;
    playerStats.maxHealth = snap.playerStats.maxHealth;
    playerStats.armor = snap.playerStats.armor;
    playerStats.maxArmor = snap.playerStats.maxArmor;
    playerStats.food = snap.playerStats.food;
    playerStats.maxFood = snap.playerStats.maxFood;
    playerStats.showSurvivalStats = snap.playerStats.showSurvivalStats;
    playerStats.isDead = snap.playerStats.isDead;
    return playerStats;
}
} // namespace

void GameplayHudPresenter::render(const GameplayPresentationSnapshot& snap,
                                   RhiDevice& rhiDevice,
                                   GameStateMachine& stateMachine) {
    const Window::FramebufferSize size = m_window.getFramebufferSize();
    UIRenderContext context = prepareRenderContext(
        snap, rhiDevice, size.width, size.height);
    renderPrepared(context, stateMachine);
}

UIRenderContext GameplayHudPresenter::prepareRenderContext(const GameplayPresentationSnapshot& snap,
                                                           RhiDevice& rhiDevice,
                                                           const int surfaceWidth,
                                                           const int surfaceHeight) {
    m_playerStats = toPlayerStatsData(snap);
    return m_uiRenderer.prepareRenderContext(
        surfaceWidth,
        surfaceHeight,
        rhiDevice,
        *snap.inventory,
        m_playerStats,
        m_input.snapshot());
}

bool GameplayHudPresenter::prepareTextFrame(RhiCommandList& commandList)
{
    return m_uiRenderer.prepareTextFrame(commandList);
}

void GameplayHudPresenter::renderPrepared(const UIRenderContext& context,
                                          GameStateMachine& stateMachine) {
    m_uiRenderer.renderPrepared(context);
    stateMachine.render();
}

#ifdef MECRAFT_DEBUG
#include "../../ecs/GameplayRegistry.h"
#include "../../world/World.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/passes/PostProcessPass.h"
#include "../../ui/Dashboard.h"

bool GameplayHudPresenter::prepareDashboard(
    RhiCommandList& commandList,
    const int framebufferWidth,
    const int framebufferHeight,
    ecs::GameplayRegistry& reg,
    World& world,
    const Camera& camera,
    RenderResourceHub& renderer,
    RenderScene& renderScene,
    PostProcessPass& postProcess,
    Dashboard::FrameProfilerStats& profilerStats,
    const std::function<void(int)>& renderDistanceSetter) {
    if (!m_dashboard) {
        return false;
    }
    Camera mutableCamera = camera;
    return m_dashboard->prepareFrame(commandList,
                                     framebufferWidth,
                                     framebufferHeight,
                                     reg, world, mutableCamera, renderer,
                                     renderScene, postProcess, m_uiRenderer,
                                     profilerStats, renderDistanceSetter);
}

void GameplayHudPresenter::recordDashboard(RhiCommandList& commandList) const {
    if (!m_dashboard) {
        std::abort();
    }
    m_dashboard->recordDraws(commandList);
}
#endif
