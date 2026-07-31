#ifndef MECRAFT_GAMEPLAYSTATE_H
#define MECRAFT_GAMEPLAYSTATE_H
#include <functional>
#include <memory>
#include <utility>

#include "IGameState.h"
#include "../modes/GameplayModeRules.h"
#include "../inventory/InventoryStateContext.h"
#include "CommandStateContext.h"
#include "GameplayStateContext.h"
#include "StateDependencies.h"

/// Main gameplay input state. The legacy StateDependencies constructor is kept
/// as a compatibility adapter; the state stores only narrow contexts.
class GameplayState : public IGameState {
public:
    using StateFactory = std::function<std::unique_ptr<IGameState>()>;

    explicit GameplayState(StateDependencies deps, const IGameplayModeRules& modeRules = SurvivalModeRules::instance(),
                           GameplayMode gameplayMode = GameplayMode::Survival);
    GameplayState(GameplayStateContext gameplayCtx, InventoryStateContext inventoryCtx, CommandStateContext commandCtx,
                  StateFactory makeCreativeModeState, StateFactory makeSurvivalModeState,
                  const IGameplayModeRules& modeRules = SurvivalModeRules::instance(),
                  GameplayMode gameplayMode = GameplayMode::Survival);

    void onEnter() override;
    void update(float dt, const InputSnapshot& snapshot) override;
    [[nodiscard]] GameStateKind kind() const override { return GameStateKind::Gameplay; }

    /// The gameplay mode represented by this state (survival or creative).
    /// Used to keep the local player's PlayerModeComponent in sync.
    [[nodiscard]] GameplayMode gameplayMode() const override { return m_gameplayMode; }

private:
    bool handleBlockContainerInteraction(const InputSnapshot& snapshot);
    bool handleInventoryTransition();
    bool handleMenuTransition();
    bool handleCommandTransition();
    void driveLegacyGameplayBridge(float dt);
    void resetBlockBreakSession();

    GameplayStateContext m_ctx;
    InventoryStateContext m_inventoryCtx;
    CommandStateContext m_commandCtx;
    StateFactory m_makeCreativeModeState;
    StateFactory m_makeSurvivalModeState;
    const IGameplayModeRules& m_modeRules;
    GameplayMode m_gameplayMode = GameplayMode::Survival;
};

#endif //MECRAFT_GAMEPLAYSTATE_H
