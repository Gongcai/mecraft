#ifndef MECRAFT_GAMEPLAYSTATE_H
#define MECRAFT_GAMEPLAYSTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "../../player/Player.h"
#include "../../world/World.h"
#include "../../world/Block.h"
#include "UIState.h"

namespace physics {
class PhysicsSystem;
}

class GameplayState : public IGameState {
public:
    GameplayState(GameStateMachine& fsm,
                  Player& player,
                  InputContextManager& ctx,
                  InputManager& input,
                  physics::PhysicsSystem& physicsSystem,
                  World& world)
            : m_fsm(fsm), m_player(player), m_context(ctx), m_input(input),
              m_physicsSystem(physicsSystem), m_world(world) {}

    void onEnter() override {
        // Ensure we are in Gameplay context
        // This might be redundant if this is the first state, but safe.
        if (m_context.getCurrentContext() != InputContextType::Gameplay) {
             m_context.switchContext(InputContextType::Gameplay);
        }
        // Gameplay requires captured mouse
        m_input.captureMouse(true);
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        // Check for global transitions (e.g. open menu)
        if (m_context.isActionTriggered(Action::Menu)) {
            m_fsm.pushState(std::make_unique<UIState>(m_fsm, m_context, m_input));
            return;
        }

        // Update gameplay entities
        m_player.update(dt, snapshot, m_context, m_physicsSystem);

        const bool wantsBreak = m_context.isActionTriggered(Action::Attack);
        const bool wantsPlace = m_context.isActionTriggered(Action::UseItem);
        if (!wantsBreak && !wantsPlace) {
            return;
        }

        glm::ivec3 hitBlock{};
        glm::ivec3 placeBlock{};
        constexpr float kPickDistance = 6.0f;
        if (!m_world.raycast(m_player.getCamera().getPickRay(), kPickDistance, hitBlock, placeBlock)) {
            return;
        }

        if (wantsBreak) {
            m_world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, BlockType::AIR);
            return;
        }

        if (wantsPlace &&
            m_world.getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockType::AIR &&
            !m_player.wouldOverlapBlock(placeBlock)) {
            m_world.setBlock(placeBlock.x, placeBlock.y, placeBlock.z, BlockType::DIRT);
        }
    }

private:
    GameStateMachine& m_fsm;
    Player& m_player;
    InputContextManager& m_context;
    InputManager& m_input;
    physics::PhysicsSystem& m_physicsSystem;
    World& m_world;
};

#endif //MECRAFT_GAMEPLAYSTATE_H

