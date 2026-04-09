#ifndef MECRAFT_GAMEPLAYSTATE_H
#define MECRAFT_GAMEPLAYSTATE_H
#include <string>
#include <random>
#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "../../player/Player.h"
#include "../../world/World.h"
#include "../../world/Block.h"
#include "../../particle/ParticleSystem.h"
#include "UIState.h"
namespace {
    std::string getRandomName(std::string name,int maxRandomLength) {
        static std::random_device rd;
        static std::mt19937 gen(rd());

        std::uniform_int_distribution<> dist(1, maxRandomLength);
        int randomNum = dist(gen);

        return name + std::to_string(randomNum);


    }
}

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
                  World& world,
                  AudioEngine& audioEngine,
                  ParticleSystem& particleSystem)
            : m_fsm(fsm), m_player(player), m_context(ctx), m_input(input),
              m_physicsSystem(physicsSystem), m_world(world),
              m_audioEngine(audioEngine), m_particleSystem(particleSystem) {}

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

        // Handle hotbar slot selection via action map
        for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i) {
            Action hotbarAction = static_cast<Action>(static_cast<int>(Action::Hotbar1) + i);
            if (m_context.isActionTriggered(hotbarAction)) {
                m_player.getInventory().setSelectedSlot(i);
            }
        }

        // Handle hotbar scroll (cyclic)
        if (m_context.isActionTriggered(Action::HotbarScrollUp)) {
            m_player.getInventory().scrollSlot(-1);
        }
        if (m_context.isActionTriggered(Action::HotbarScrollDown)) {
            m_player.getInventory().scrollSlot(1);
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

        if (m_player.isMoving()) {
            float stepInterval = m_player.isSprinting() ? 0.35f : 0.5f;
            m_footstepTimer -= dt;
            if (m_footstepTimer <= 0.0f) {
                // 随机选择脚步声
                std::string soundName = "walk_grass" + std::to_string(m_footstepIndex + 1);
                m_audioEngine.playSound2D(soundName, 1.f);
                m_footstepIndex = (m_footstepIndex + 1) % 6;
                m_footstepTimer = stepInterval;
            }
        }
        if (m_player.isJustLanded()) {
            m_audioEngine.playClip(getRandomName("grass",4),m_player.getPosition());

        }

        const bool wantsBreak = m_context.isActionTriggered(Action::Attack);
        const bool wantsPlace = m_context.isActionHeld(Action::UseItem);
        if (!hasHit || (!wantsBreak && !wantsPlace)) {
            return;
        }



        if (wantsBreak) {
            BlockID brokenBlock = m_world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            m_world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, BlockType::AIR);
            m_audioEngine.playClip(getRandomName("put",5),hitBlock);
            m_particleSystem.emit(hitBlock, brokenBlock);
            return;
        }

        if (wantsPlace &&
            m_placeCooldownRemaining <= 0.0f &&
            m_world.getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockType::AIR &&
            !m_player.wouldOverlapBlock(placeBlock)) {

            m_world.setBlock(placeBlock.x, placeBlock.y, placeBlock.z, m_player.getInventory().getSelectedBlock());
            m_placeCooldownRemaining = kPlaceCooldownSec;
            m_audioEngine.playClip(getRandomName("put",5),placeBlock);
        }




    }

private:
    GameStateMachine& m_fsm;
    Player& m_player;
    InputContextManager& m_context;
    InputManager& m_input;
    physics::PhysicsSystem& m_physicsSystem;
    World& m_world;
    AudioEngine& m_audioEngine;
    ParticleSystem& m_particleSystem;
    static constexpr float kPlaceCooldownSec = 0.18f;
    float m_placeCooldownRemaining = 0.0f;

    float m_footstepTimer = 0.0f;
    int m_footstepIndex = 0;
};

#endif //MECRAFT_GAMEPLAYSTATE_H

