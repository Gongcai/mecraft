#include "GameplayState.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

#include "GameStateMachine.h"
#include "engine/input/InputContextManager.h"
#include "CommandState.h"
#include "GameplayStateEcsBridge.h"
#include "SleepingState.h"
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
#include "../../world/World.h"
#include "../../world/block/BedBlock.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/block/PropIndices.h"

namespace {

bool isRedstoneControlBlock(const BlockID blockId)
{
    return blockId == BlockIds::REPEATER || blockId == BlockIds::COMPARATOR;
}

void requireRepeaterDelayProperties()
{
    if (PropIndices::DELAY == PropIndices::INVALID ||
        PropIndices::DELAY_1 == PropIndices::INVALID ||
        PropIndices::DELAY_2 == PropIndices::INVALID ||
        PropIndices::DELAY_3 == PropIndices::INVALID ||
        PropIndices::DELAY_4 == PropIndices::INVALID) {
        throw std::runtime_error("Repeater interaction requires registered delay property values");
    }
}

void requireComparatorModeProperties()
{
    if (PropIndices::MODE == PropIndices::INVALID ||
        PropIndices::MODE_COMPARE == PropIndices::INVALID ||
        PropIndices::MODE_SUBTRACT == PropIndices::INVALID) {
        throw std::runtime_error("Comparator interaction requires registered mode property values");
    }
}

uint16_t nextRepeaterDelayValue(const uint16_t currentDelay)
{
    if (currentDelay == PropIndices::DELAY_1) {
        return PropIndices::DELAY_2;
    }
    if (currentDelay == PropIndices::DELAY_2) {
        return PropIndices::DELAY_3;
    }
    if (currentDelay == PropIndices::DELAY_3) {
        return PropIndices::DELAY_4;
    }
    if (currentDelay == PropIndices::DELAY_4) {
        return PropIndices::DELAY_1;
    }
    throw std::runtime_error("Repeater state contains an unknown delay value");
}

uint16_t toggledComparatorModeValue(const uint16_t currentMode)
{
    if (currentMode == PropIndices::MODE_COMPARE) {
        return PropIndices::MODE_SUBTRACT;
    }
    if (currentMode == PropIndices::MODE_SUBTRACT) {
        return PropIndices::MODE_COMPARE;
    }
    throw std::runtime_error("Comparator state contains an unknown mode value");
}

StateID nextRedstoneControlState(const StateID currentState)
{
    const BlockID blockId = BlockStateRegistry::getBlockId(currentState);
    if (blockId == BlockIds::REPEATER) {
        requireRepeaterDelayProperties();
        const uint16_t currentDelay = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::DELAY);
        if (currentDelay == PropIndices::INVALID) {
            throw std::runtime_error("Repeater state is missing the delay property");
        }
        const StateID updatedState = BlockStateRegistry::withProperty(
            currentState,
            PropIndices::DELAY,
            nextRepeaterDelayValue(currentDelay));
        if (updatedState == currentState) {
            throw std::runtime_error("Repeater delay state transition failed");
        }
        return updatedState;
    }

    if (blockId == BlockIds::COMPARATOR) {
        requireComparatorModeProperties();
        const uint16_t currentMode = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::MODE);
        if (currentMode == PropIndices::INVALID) {
            throw std::runtime_error("Comparator state is missing the mode property");
        }
        const StateID updatedState = BlockStateRegistry::withProperty(
            currentState,
            PropIndices::MODE,
            toggledComparatorModeValue(currentMode));
        if (updatedState == currentState) {
            throw std::runtime_error("Comparator mode state transition failed");
        }
        return updatedState;
    }

    throw std::runtime_error("Unsupported redstone control block interaction");
}

} // namespace

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
        if (isRedstoneControlBlock(targetBlock)) {
            const StateID updatedState = nextRedstoneControlState(targetState);
            m_ctx.world->setBlockState(
                target.targetBlock.x,
                target.targetBlock.y,
                target.targetBlock.z,
                updatedState);
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

        const BlockID craftingTableBlock = BlockRegistry::findByName("minecraft:crafting_table");
        if (targetBlock == craftingTableBlock) {
            m_ctx.fsm.pushState(std::make_unique<WorkbenchState>(m_inventoryCtx));
            return true;
        }

        const BlockID furnaceBlock = BlockRegistry::findByName("minecraft:furnace");
        if (targetBlock == furnaceBlock) {
            m_ctx.fsm.pushState(std::make_unique<FurnaceState>(m_inventoryCtx, target.targetBlock));
            return true;
        }

        if (targetBlock == BlockIds::CHEST) {
            m_ctx.fsm.pushState(std::make_unique<ChestInventoryState>(m_inventoryCtx, target.targetBlock));
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
