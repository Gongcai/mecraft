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

    // Convert snapshot held item motion to UI format
    HeldItemPreviewMotion motion;
    motion.moving = snap.heldItemMotion.moving;
    motion.sprinting = snap.heldItemMotion.sprinting;
    motion.bobFrequency = snap.heldItemMotion.bobFrequency;
    motion.bobPhaseOffset = snap.heldItemMotion.bobPhaseOffset;
    motion.cameraYawDegrees = snap.heldItemMotion.cameraYawDegrees;
    motion.cameraPitchDegrees = snap.heldItemMotion.cameraPitchDegrees;

    m_uiRenderer.render(m_window, *snap.inventory, playerStats, motion, m_input.snapshot());
    stateMachine.render();
}
