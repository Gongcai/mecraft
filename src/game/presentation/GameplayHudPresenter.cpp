#include "GameplayHudPresenter.h"
#include "../../ui/core/UIRenderer.h"
#include "../states/GameStateMachine.h"
#include "../../engine/input/InputManager.h"

void GameplayHudPresenter::render(const GameplayPresentationSnapshot& snap,
                                   GameStateMachine& stateMachine) {
    // Convert snapshot player stats to UI format
    PlayerStatsData playerStats;
    playerStats.health = snap.playerStats.health;
    playerStats.maxHealth = snap.playerStats.maxHealth;
    playerStats.armor = snap.playerStats.armor;
    playerStats.maxArmor = snap.playerStats.maxArmor;
    playerStats.food = snap.playerStats.food;
    playerStats.maxFood = snap.playerStats.maxFood;
    playerStats.showSurvivalStats = snap.playerStats.showSurvivalStats;

    m_uiRenderer.render(m_window, *snap.inventory, playerStats, m_input.snapshot());
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
