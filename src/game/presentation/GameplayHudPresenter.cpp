#include "GameplayHudPresenter.h"
#include "../../ui/core/UIRenderer.h"
#include "../states/GameStateMachine.h"
#include "../../engine/input/InputManager.h"

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
                                   GameStateMachine& stateMachine) {
    UIRenderContext context = prepareRenderContext(snap);
    renderPrepared(context, stateMachine);
}

UIRenderContext GameplayHudPresenter::prepareRenderContext(const GameplayPresentationSnapshot& snap) {
    const PlayerStatsData playerStats = toPlayerStatsData(snap);
    return m_uiRenderer.prepareRenderContext(m_window, *snap.inventory, playerStats, m_input.snapshot());
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

void GameplayHudPresenter::renderDashboard(ecs::GameplayRegistry& reg,
                                             World& world,
                                             const Camera& camera,
                                             RenderResourceHub& renderer,
                                             RenderScene& renderScene,
                                             PostProcessPass& postProcess,
                                             Dashboard::FrameProfilerStats& profilerStats,
                                             const std::function<void(int)>& renderDistanceSetter) {
    if (!m_dashboard) {
        return;
    }
    Camera mutableCamera = camera;
    m_dashboard->render(reg, world, mutableCamera, renderer, renderScene,
                        postProcess, m_uiRenderer, profilerStats, renderDistanceSetter);
}
#endif
