#ifndef MECRAFT_GAMEPLAYSTATE_H
#define MECRAFT_GAMEPLAYSTATE_H
#include <string>

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "GameplayModeRules.h"
#include "InventoryState.h"
#include "StateDependencies.h"
#include "../../player/Player.h"
#include "../../world/World.h"
#include "../../world/Block.h"
#include "../../world/DropSystem.h"
#include "../../item/Item.h"
#include "../../particle/ParticleSystem.h"
#include "../../ui/UIRenderer.h"
#include "CommandState.h"
#include "UIState.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/util/GameplayRuntimeContext.h"
#include "../../ecs/components/Components.h"

namespace physics {
class PhysicsSystem;
}

class GameplayState : public IGameState {
public:
    explicit GameplayState(StateDependencies deps,
                           const IGameplayModeRules& modeRules = SurvivalModeRules::instance(),
                           GameplayMode gameplayMode = GameplayMode::Survival)
            : m_deps(deps),
              m_modeRules(modeRules),
              m_gameplayMode(gameplayMode) {}

    void onEnter() override {
        if (m_deps.context.getCurrentContext() != InputContextType::Gameplay) {
             m_deps.context.switchContext(InputContextType::Gameplay);
        }
        m_deps.input.captureMouse(true);
        m_deps.uiRenderer.setInventoryPanelVisible(false);
        m_deps.input.clearUIDragItem();

        if (!m_deps.ecsRegistry.ctxHas<ecs::GameplayRuntimeContext>()) {
            m_deps.ecsRegistry.ctxSet<ecs::GameplayRuntimeContext>();
        }
        m_deps.ecsRegistry.ctxGet<ecs::GameplayRuntimeContext>().modeRules = &m_modeRules;

        auto view = m_deps.ecsRegistry.view<ecs::LocalPlayerTag, ecs::InventoryComponent>();
        for (auto e : view) {
            view.get<ecs::InventoryComponent>(e).selectedHotbarSlot = m_deps.player.getInventory().getSelectedSlot();
        }

    }

    void update(float dt, const InputSnapshot& snapshot) override {
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

private:
    bool handleInventoryTransition() {
        if (!m_deps.context.isActionTriggered(Action::Inventory)) {
            return false;
        }
        m_deps.fsm.pushState(std::make_unique<InventoryState>(m_deps, m_gameplayMode));
        return true;
    }

    bool handleMenuTransition() {
        if (!m_deps.context.isActionTriggered(Action::Menu)) {
            return false;
        }

        m_deps.fsm.pushState(std::make_unique<UIState>(m_deps));
        return true;
    }

    bool handleCommandTransition() {
        if (!m_deps.context.isActionTriggered(Action::OpenCommand)) {
            return false;
        }

        m_deps.fsm.pushState(std::make_unique<CommandState>(m_deps));
        return true;
    }

    void driveLegacyGameplayBridge(float dt) {
        static_cast<void>(dt);
    }

    void resetBlockBreakSession() {
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
        m_deps.player.clearBlockBreakProgress();
        m_deps.uiRenderer.setHeldItemPreviewActionAnimationActive(false);
    }


    StateDependencies m_deps;
    const IGameplayModeRules& m_modeRules;
    GameplayMode m_gameplayMode = GameplayMode::Survival;
};

#endif //MECRAFT_GAMEPLAYSTATE_H
