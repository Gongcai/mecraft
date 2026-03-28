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
        if (m_placeCooldownRemaining > 0.0f) {
            m_placeCooldownRemaining -= dt;
            if (m_placeCooldownRemaining < 0.0f) {
                m_placeCooldownRemaining = 0.0f;
            }
        }

        // Check for global transitions (e.g. open menu)
        if (m_context.isActionTriggered(Action::Menu)) {
            m_fsm.pushState(std::make_unique<UIState>(m_fsm, m_context, m_input));
            return;
        }

        // Update gameplay entities
        m_player.update(dt, snapshot, m_context, m_physicsSystem);

        glm::ivec3 hitBlock{};
        glm::ivec3 placeBlock{};
        constexpr float kPickDistance = 6.0f;
        const bool hasHit = m_world.raycast(m_player.getCamera().getPickRay(), kPickDistance, hitBlock, placeBlock);
        if (hasHit) {
            m_player.setTargetBlock(hitBlock);
        } else {
            m_player.clearTargetBlock();
        }

        const bool wantsBreak = m_context.isActionTriggered(Action::Attack);
        const bool wantsPlace = m_context.isActionHeld(Action::UseItem);
        if (!hasHit || (!wantsBreak && !wantsPlace)) {
            return;
        }

        if (wantsBreak) {
            m_world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, BlockType::AIR);
            return;
        }

        if (wantsPlace &&
            m_placeCooldownRemaining <= 0.0f &&
            m_world.getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockType::AIR &&
            !m_player.wouldOverlapBlock(placeBlock)) {
            m_world.setBlock(placeBlock.x, placeBlock.y, placeBlock.z, BlockType::DIRT);
            m_placeCooldownRemaining = kPlaceCooldownSec;
        }
    }

private:
    GameStateMachine& m_fsm;
    Player& m_player;
    InputContextManager& m_context;
    InputManager& m_input;
    physics::PhysicsSystem& m_physicsSystem;
    World& m_world;
    static constexpr float kPlaceCooldownSec = 0.18f;
    float m_placeCooldownRemaining = 0.0f;
};

#endif //MECRAFT_GAMEPLAYSTATE_H

