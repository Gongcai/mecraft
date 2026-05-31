#include "GameplayState.h"

#include <memory>

#include "GameStateMachine.h"
#include "engine//input/InputContextManager.h"
#include "CommandState.h"
#include "../inventory/CreativeInventoryState.h"
#include "../inventory/InventoryState.h"
#include "UIState.h"
#include "UIStateContext.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/components/Components.h"
#include "../../ecs/util/GameplayRuntimeContext.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIRenderer.h"

GameplayState::GameplayState(StateDependencies deps,
                             const IGameplayModeRules& modeRules,
                             const GameplayMode gameplayMode)
    : m_deps(deps),
      m_modeRules(modeRules),
      m_gameplayMode(gameplayMode) {}

void GameplayState::onEnter()
{
    if (m_deps.context.getCurrentContext() != InputContextType::Gameplay) {
         m_deps.context.switchContext(InputContextType::Gameplay);
    }
    m_deps.input.captureMouse(true);
    m_deps.uiRenderer.setInventoryPanelVisible(false);
    m_deps.uiRenderer.setCreativeInventoryVisible(false);
    m_deps.input.clearUIDragItem();

    if (!m_deps.ecsRegistry.ctxHas<ecs::GameplayRuntimeContext>()) {
        m_deps.ecsRegistry.ctxSet<ecs::GameplayRuntimeContext>();
    }
    auto& runtime = m_deps.ecsRegistry.ctxGet<ecs::GameplayRuntimeContext>();
    runtime.modeRules = &m_modeRules;
    runtime.gameplayMode = m_gameplayMode;

    auto view = m_deps.ecsRegistry.view<ecs::LocalPlayerTag, ecs::InventoryComponent>();
    for (auto e : view) {
        view.get<ecs::InventoryComponent>(e).selectedHotbarSlot = m_deps.inventory.getSelectedSlot();
    }
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
    if (!m_deps.context.isActionTriggered(Action::Inventory)) {
        return false;
    }
    if (m_gameplayMode == GameplayMode::Creative) {
        m_deps.fsm.pushState(std::make_unique<CreativeInventoryState>(m_deps));
        return true;
    }
    m_deps.fsm.pushState(std::make_unique<InventoryState>(m_deps, m_gameplayMode));
    return true;
}

bool GameplayState::handleMenuTransition()
{
    if (!m_deps.context.isActionTriggered(Action::Menu)) {
        return false;
    }

    // G6: Create UIState with narrow UIStateContext
    UIStateContext uiCtx{m_deps.fsm, m_deps.context, m_deps.input, m_deps.uiRenderer, m_deps.localeManager};
    m_deps.fsm.pushState(std::make_unique<UIState>(uiCtx));
    return true;
}

bool GameplayState::handleCommandTransition()
{
    if (!m_deps.context.isActionTriggered(Action::OpenCommand)) {
        return false;
    }

    m_deps.fsm.pushState(std::make_unique<CommandState>(m_deps));
    return true;
}

void GameplayState::driveLegacyGameplayBridge(float dt)
{
    static_cast<void>(dt);
}

void GameplayState::resetBlockBreakSession()
{
    auto view = m_deps.ecsRegistry.view<ecs::LocalPlayerTag>();
    for (auto e : view) {
        if (m_deps.ecsRegistry.has<ecs::BlockBreakComponent>(e)) {
            auto& blockBreak = m_deps.ecsRegistry.get<ecs::BlockBreakComponent>(e);
            blockBreak.active = false;
            blockBreak.blockPos = glm::ivec3{};
            blockBreak.progress01 = 0.0f;
        }
        if (m_deps.ecsRegistry.has<ecs::BlockInteractionRuntimeComponent>(e)) {
            auto& runtime = m_deps.ecsRegistry.get<ecs::BlockInteractionRuntimeComponent>(e);
            runtime.breakActive = false;
            runtime.breakBlockPos = glm::ivec3{};
            runtime.breakElapsedMs = 0.0f;
            runtime.breakRequiredMs = 0.0f;
        }
    }
    m_deps.uiRenderer.setHeldItemPreviewActionAnimationActive(false);
}
