#ifndef MECRAFT_GAMEPLAYSTATE_H
#define MECRAFT_GAMEPLAYSTATE_H
#include "IGameState.h"
#include "../modes/GameplayModeRules.h"
#include "StateDependencies.h"

class GameplayState : public IGameState {
public:
    explicit GameplayState(StateDependencies deps,
                           const IGameplayModeRules& modeRules = SurvivalModeRules::instance(),
                           GameplayMode gameplayMode = GameplayMode::Survival);

    void onEnter() override;
    void update(float dt, const InputSnapshot& snapshot) override;

private:
    bool handleInventoryTransition();
    bool handleMenuTransition();
    bool handleCommandTransition();
    void driveLegacyGameplayBridge(float dt);
    void resetBlockBreakSession();

    StateDependencies m_deps;
    const IGameplayModeRules& m_modeRules;
    GameplayMode m_gameplayMode = GameplayMode::Survival;
};

#endif //MECRAFT_GAMEPLAYSTATE_H
