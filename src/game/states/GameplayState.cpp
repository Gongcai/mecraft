#include "GameplayState.h"

#include <memory>

#include "GameStateMachine.h"
#include "engine//input/InputContextManager.h"
#include "CommandState.h"
#include "GameplayStateEcsBridge.h"
#include "../inventory/CreativeInventoryState.h"
#include "../inventory/InventoryState.h"
#include "../modes/CreativeModeState.h"
#include "UIState.h"
#include "UIStateContext.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/util/GameplayRuntimeContext.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIRenderer.h"

GameplayState::GameplayState(StateDependencies deps,
                             const IGameplayModeRules& modeRules,
                             const GameplayMode gameplayMode)
    : GameplayState(
          GameplayStateContext{
              deps.fsm,
              deps.context,
              deps.input,
              deps.uiRenderer,
              deps.ecsRegistry,
              deps.inventory,
              deps.localeManager
          },
          InventoryStateContext{
              deps.fsm,
              deps.inventory,
              deps.context,
              deps.input,
              deps.uiRenderer,
              deps.dropSystem,
              deps.ecsRegistry
          },
          CommandStateContext{
              deps.fsm,
              deps.context,
              deps.input,
              deps.uiRenderer,
              deps.lastSubmittedCommand,
              deps.world,
              deps.ecsRegistry,
              deps.localeManager,
              deps.gameClient,
              deps.isMultiplayer
          },
          [deps]() -> std::unique_ptr<IGameState> { return std::make_unique<CreativeModeState>(deps); },
          [deps]() -> std::unique_ptr<IGameState> { return std::make_unique<GameplayState>(deps); },
          modeRules,
          gameplayMode) {}

GameplayState::GameplayState(GameplayStateContext gameplayCtx,
                             InventoryStateContext inventoryCtx,
                             CommandStateContext commandCtx,
                             StateFactory makeCreativeModeState,
                             StateFactory makeSurvivalModeState,
                             const IGameplayModeRules& modeRules,
                             const GameplayMode gameplayMode)
    : m_ctx(gameplayCtx),
      m_inventoryCtx(inventoryCtx),
      m_commandCtx(commandCtx),
      m_makeCreativeModeState(std::move(makeCreativeModeState)),
      m_makeSurvivalModeState(std::move(makeSurvivalModeState)),
      m_modeRules(modeRules),
      m_gameplayMode(gameplayMode) {}

void GameplayState::onEnter()
{
    if (m_ctx.context.getCurrentContext() != InputContextType::Gameplay) {
         m_ctx.context.switchContext(InputContextType::Gameplay);
    }
    m_ctx.input.captureMouse(true);
    m_ctx.uiRenderer.setInventoryPanelVisible(false);
    m_ctx.uiRenderer.setCreativeInventoryVisible(false);
    m_ctx.input.clearUIDragItem();

    if (!m_ctx.ecsRegistry.ctxHas<ecs::GameplayRuntimeContext>()) {
        m_ctx.ecsRegistry.ctxSet<ecs::GameplayRuntimeContext>();
    }
    auto& runtime = m_ctx.ecsRegistry.ctxGet<ecs::GameplayRuntimeContext>();
    runtime.modeRules = &m_modeRules;
    runtime.gameplayMode = m_gameplayMode;

    GameplayStateEcsBridge::syncSelectedHotbarSlot(m_ctx.ecsRegistry, m_ctx.inventory);
}

void GameplayState::update(float dt, const InputSnapshot& snapshot)
{
    static_cast<void>(snapshot);
    if (handleInventoryTransition()) {
        resetBlockBreakSession();
        return;
    }
    if (handleCommandTransition()) {
        resetBlockBreakSession();
        return;
    }
    if (handleMenuTransition()) {
        resetBlockBreakSession();
        return;
    }

    driveLegacyGameplayBridge(dt);
}

bool GameplayState::handleInventoryTransition()
{
    if (!m_ctx.context.isActionTriggered(Action::Inventory)) {
        return false;
    }
    if (m_gameplayMode == GameplayMode::Creative) {
        m_ctx.fsm.pushState(std::make_unique<CreativeInventoryState>(m_inventoryCtx));
        return true;
    }
    m_ctx.fsm.pushState(std::make_unique<InventoryState>(m_inventoryCtx, m_gameplayMode));
    return true;
}

bool GameplayState::handleMenuTransition()
{
    if (!m_ctx.context.isActionTriggered(Action::Menu)) {
        return false;
    }

    // G6: Create UIState with narrow UIStateContext
    UIStateContext uiCtx{m_ctx.fsm, m_ctx.context, m_ctx.input, m_ctx.uiRenderer, m_ctx.localeManager};
    m_ctx.fsm.pushState(std::make_unique<UIState>(uiCtx));
    return true;
}

bool GameplayState::handleCommandTransition()
{
    if (!m_ctx.context.isActionTriggered(Action::OpenCommand)) {
        return false;
    }

    m_ctx.fsm.pushState(std::make_unique<CommandState>(
        m_commandCtx,
        m_makeCreativeModeState,
        m_makeSurvivalModeState));
    return true;
}

void GameplayState::driveLegacyGameplayBridge(float dt)
{
    static_cast<void>(dt);
}

void GameplayState::resetBlockBreakSession()
{
    GameplayStateEcsBridge::resetBlockBreakSession(m_ctx.ecsRegistry);
}
