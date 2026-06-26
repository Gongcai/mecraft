#include "GameplayState.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

#include "GameStateMachine.h"
#include "engine/input/InputContextManager.h"
#include "CommandState.h"
#include "GameplayStateEcsBridge.h"
#include "SleepingState.h"
#include "../redstone/RedstoneControlInteraction.h"
#include "../inventory/ChestInventoryState.h"
#include "../inventory/CreativeInventoryState.h"
#include "../inventory/FurnaceState.h"
#include "../inventory/InventoryState.h"
#include "../inventory/WorkbenchState.h"
#include "../modes/CreativeModeState.h"
#include "UIState.h"
#include "UIStateContext.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/components/Components.h"
#include "../../ecs/util/GameplayRuntimeContext.h"
#include "../../client/GameClient.h"
#include "../../locale/LocaleManager.h"
#include "../../player/Inventory.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ui/inventory/ContainerUiRegistry.h"
#include "../../world/World.h"
#include "../../world/block/BedBlock.h"
#include "../../world/block/DoorBlock.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/block/PropIndices.h"

namespace {
void pushContainerUiState(GameplayStateContext& gameplayCtx,
                          const InventoryStateContext& inventoryCtx,
                          const std::string& containerUi,
                          const glm::ivec3& blockPosition) {
    const ui::ContainerUiDef& def = ui::ContainerUiRegistry::require(containerUi);
    if (def.behavior == "crafting_table") {
        gameplayCtx.fsm.pushState(std::make_unique<WorkbenchState>(inventoryCtx));
        return;
    }
    if (def.behavior == "furnace") {
        gameplayCtx.fsm.pushState(std::make_unique<FurnaceState>(inventoryCtx, blockPosition));
        return;
    }
    if (def.behavior == "chest") {
        gameplayCtx.fsm.pushState(std::make_unique<ChestInventoryState>(inventoryCtx, blockPosition));
        return;
    }
    throw std::runtime_error("Unknown container UI behavior for block interaction: " + def.behavior);
}
}

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
              deps.localeManager,
              deps.renderScene,
              &deps.world,
              [&world = deps.world, &client = deps.gameClient](const int distance) {
                  world.setRenderDistance(distance);
                  client.clientWorld().setRenderDistance(distance);
                  client.sendViewConfig(distance);
              },
              deps.isMultiplayer
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
    m_ctx.uiRenderer.setChestPanelVisible(false);
    m_ctx.uiRenderer.setFurnacePanelVisible(false);
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
    if (handleBlockContainerInteraction(snapshot)) {
        resetBlockBreakSession();
        return;
    }
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

bool GameplayState::handleBlockContainerInteraction(const InputSnapshot& snapshot)
{
    if (!m_ctx.context.isActionTriggered(Action::UseItem) ||
        !snapshot.isMouseButtonJustPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
        return false;
    }
    if (m_ctx.context.isActionHeld(Action::Crouch)) {
        return false;
    }

    auto view = m_ctx.ecsRegistry.view<ecs::LocalPlayerTag,
                                       ecs::BlockTargetComponent,
                                       ecs::BlockInteractionRuntimeComponent>();
    for (auto entity : view) {
        const auto& target = view.get<ecs::BlockTargetComponent>(entity);
        auto& runtime = view.get<ecs::BlockInteractionRuntimeComponent>(entity);
        if (!target.hasTarget) {
            continue;
        }
        if (runtime.postPlaceInteractionSuppressSeconds > 0.0f &&
            runtime.recentlyPlacedBlock == target.targetBlock) {
            continue;
        }

        if (m_ctx.world == nullptr) {
            throw std::runtime_error("Block interaction requires an active world context");
        }

        const StateID targetState = m_ctx.world->getBlockState(
            target.targetBlock.x,
            target.targetBlock.y,
            target.targetBlock.z);
        const BlockID targetBlock = BlockStateRegistry::getBlockId(targetState);
        if (game::redstone::isControlBlock(targetBlock)) {
            const StateID updatedState = game::redstone::nextControlState(targetState);
            if (DoorBlockLogic::isDoorState(targetState)) {
                DoorBlockLogic::setDoorOpen(
                    *m_ctx.world,
                    target.targetBlock,
                    BlockStateRegistry::getPropertyIndex(updatedState, PropIndices::OPEN) == PropIndices::OPEN_TRUE);
            } else {
                m_ctx.world->setBlockState(
                    target.targetBlock.x,
                    target.targetBlock.y,
                    target.targetBlock.z,
                    updatedState);
            }
            runtime.placeCooldownRemaining = std::max(runtime.placeCooldownRemaining,
                                                      m_modeRules.placeCooldownSeconds());
            return true;
        }

        if (BedBlockLogic::isBedState(targetState)) {
            runtime.placeCooldownRemaining = std::max(runtime.placeCooldownRemaining,
                                                      m_modeRules.placeCooldownSeconds());
            if (m_ctx.isMultiplayer) {
                m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("sleep_singleplayer_only"));
                return true;
            }
            if (!m_ctx.world->getDayNightSystem().isNightTime()) {
                m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("sleep_night_only"));
                return true;
            }
            m_ctx.fsm.pushState(std::make_unique<SleepingState>(
                m_ctx.fsm,
                m_ctx.context,
                m_ctx.input,
                *m_ctx.world));
            return true;
        }

        const BlockDef& targetDef = BlockRegistry::getFast(targetBlock);
        if (!targetDef.containerUi.empty()) {
            pushContainerUiState(m_ctx, m_inventoryCtx, targetDef.containerUi, target.targetBlock);
            return true;
        }
    }

    return false;
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
    UIStateContext uiCtx{m_ctx.fsm, m_ctx.context, m_ctx.input, m_ctx.uiRenderer, m_ctx.localeManager,
                         m_ctx.renderScene, m_ctx.world, m_ctx.renderDistanceSetter};
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
