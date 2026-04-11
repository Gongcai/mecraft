#ifndef MECRAFT_GAMEPLAYSTATE_H
#define MECRAFT_GAMEPLAYSTATE_H
#include <string>
#include <random>
#include <algorithm>
#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "GameplayModeRules.h"
#include "../../player/Player.h"
#include "../../world/World.h"
#include "../../world/Block.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../ui/UIRenderer.h"
#include "CommandState.h"
#include "UIState.h"
namespace gameplay_state_detail {
    inline std::string getRandomName(const std::string& name, int maxRandomLength) {
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
                  UIRenderer& uiRenderer,
                  std::string& lastSubmittedCommand,
                  physics::PhysicsSystem& physicsSystem,
                  World& world,
                  AudioEngine& audioEngine,
                  ParticleSystem& particleSystem,
                  DropSystem& dropSystem,
                  const IGameplayModeRules& modeRules = SurvivalModeRules::instance())
            : m_fsm(fsm), m_player(player), m_context(ctx), m_input(input),
              m_uiRenderer(uiRenderer), m_lastSubmittedCommand(lastSubmittedCommand),
              m_physicsSystem(physicsSystem), m_world(world),
              m_audioEngine(audioEngine), m_particleSystem(particleSystem),
              m_dropSystem(dropSystem),
              m_modeRules(modeRules) {}

    void onEnter() override {
        // Ensure we are in Gameplay context
        // This might be redundant if this is the first state, but safe.
        if (m_context.getCurrentContext() != InputContextType::Gameplay) {
             m_context.switchContext(InputContextType::Gameplay);
        }
        // Gameplay requires to be captured mouse
        m_input.captureMouse(true);
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        updatePlaceCooldown(dt);
        handleHotbarInput();
        if (handleCommandTransition()) {
            resetBlockBreakSession();
            return;
        }
        if (handleMenuTransition()) {
            resetBlockBreakSession();
            return;
        }

        const BlockSelection selection = updatePlayerAndTarget(dt, snapshot);
        updateMovementAudio(dt);
        handleBlockInteraction(dt, selection);
    }

private:
    struct BlockSelection {
        bool hasHit = false;
        glm::ivec3 hitBlock{};
        glm::ivec3 placeBlock{};
    };

    struct BlockBreakSession {
        bool active = false;
        glm::ivec3 blockPos{};
        float elapsedMs = 0.0f;
        float requiredMs = 0.0f;
    };

    void updatePlaceCooldown(float dt) {
        if (m_placeCooldownRemaining <= 0.0f) {
            return;
        }

        m_placeCooldownRemaining -= dt;
        if (m_placeCooldownRemaining < 0.0f) {
            m_placeCooldownRemaining = 0.0f;
        }
    }

    void handleHotbarInput() {
        for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i) {
            const auto hotbarAction = static_cast<Action>(static_cast<int>(Action::Hotbar1) + i);
            if (m_context.isActionTriggered(hotbarAction)) {
                m_player.getInventory().setSelectedSlot(i);
            }
        }

        if (m_context.isActionTriggered(Action::HotbarScrollUp)) {
            m_player.getInventory().scrollSlot(-1);
        }
        if (m_context.isActionTriggered(Action::HotbarScrollDown)) {
            m_player.getInventory().scrollSlot(1);
        }
    }

    bool handleMenuTransition() {
        if (!m_context.isActionTriggered(Action::Menu)) {
            return false;
        }

        m_fsm.pushState(std::make_unique<UIState>(m_fsm, m_context, m_input));
        return true;
    }

    bool handleCommandTransition() {
        if (!m_context.isActionTriggered(Action::OpenCommand)) {
            return false;
        }

        m_fsm.pushState(std::make_unique<CommandState>(
            m_fsm,
            m_context,
            m_input,
            m_uiRenderer,
            m_lastSubmittedCommand
        ));
        return true;
    }

    BlockSelection updatePlayerAndTarget(float dt, const InputSnapshot& snapshot) {
        m_player.update(dt, snapshot, m_context, m_physicsSystem);

        BlockSelection selection;
        selection.hasHit = m_world.raycast(m_player.getCamera().getPickRay(), kPickDistance,
                                           selection.hitBlock, selection.placeBlock);
        if (selection.hasHit) {
            m_player.setTargetBlock(selection.hitBlock);
        } else {
            m_player.clearTargetBlock();
        }

        return selection;
    }

    void updateMovementAudio(float dt) {
        if (m_player.isMoving()) {
            const float stepInterval = m_player.isSprinting() ? 0.35f : 0.5f;
            m_footstepTimer -= dt;
            if (m_footstepTimer <= 0.0f) {
                const std::string soundName = "walk_grass" + std::to_string(m_footstepIndex + 1);
                m_audioEngine.playSound2D(soundName, 1.f);
                m_footstepIndex = (m_footstepIndex + 1) % 6;
                m_footstepTimer = stepInterval;
            }
        }

        if (m_player.isJustLanded()) {
            m_audioEngine.playClip(gameplay_state_detail::getRandomName("grass", 4), m_player.getPosition());
        }
    }

    void resetBlockBreakSession() {
        m_blockBreakSession = {};
        m_player.clearBlockBreakProgress();
    }

    void updateBreakProgress(const glm::ivec3& blockPos, const float progress01) {
        m_player.setBlockBreakProgress(blockPos, std::clamp(progress01, 0.0f, 1.0f));
    }

    void handleBlockInteraction(float dt, const BlockSelection& selection) {
        const bool wantsBreak = m_context.isActionHeld(Action::Attack);
        const bool wantsPlace = m_context.isActionHeld(Action::UseItem);
        if (!wantsBreak && !wantsPlace) {
            resetBlockBreakSession();
            return;
        }

        GameplayBlockActionRequest request;
        request.hasHit = selection.hasHit;
        request.wantsBreak = wantsBreak;
        request.wantsPlace = wantsPlace;
        request.placeCooldownRemaining = m_placeCooldownRemaining;
        if (selection.hasHit) {
            request.targetBlock = m_world.getBlock(selection.placeBlock.x, selection.placeBlock.y, selection.placeBlock.z);
            request.playerWouldOverlapPlaceBlock = m_player.wouldOverlapBlock(selection.placeBlock);
        }

        const GameplayBlockAction action = m_modeRules.decideBlockAction(request);
        if (action == GameplayBlockAction::Break) {
            const BlockID targetBlock = m_world.getBlock(selection.hitBlock.x, selection.hitBlock.y, selection.hitBlock.z);
            if (targetBlock == BlockType::AIR || !BlockRegistry::get(targetBlock).isSelectable) {
                resetBlockBreakSession();
                return;
            }

            const float requiredMs = std::max(1.0f, static_cast<float>(BlockRegistry::get(targetBlock).timeToBreak));
            if (!m_blockBreakSession.active || m_blockBreakSession.blockPos != selection.hitBlock) {
                m_blockBreakSession.active = true;
                m_blockBreakSession.blockPos = selection.hitBlock;
                m_blockBreakSession.elapsedMs = 0.0f;
                m_blockBreakSession.requiredMs = requiredMs;
            }

            m_blockBreakSession.elapsedMs += dt * 1000.0f;
            const float progress = m_blockBreakSession.elapsedMs / m_blockBreakSession.requiredMs;
            updateBreakProgress(selection.hitBlock, progress);

            if (m_blockBreakSession.elapsedMs < m_blockBreakSession.requiredMs) {
                return;
            }

            const BlockID brokenBlock = m_world.getBlock(selection.hitBlock.x, selection.hitBlock.y, selection.hitBlock.z);
            m_world.setBlock(selection.hitBlock.x, selection.hitBlock.y, selection.hitBlock.z, BlockType::AIR);
            m_dropSystem.spawnBlockDrop(brokenBlock, selection.hitBlock);
            m_audioEngine.playClip(gameplay_state_detail::getRandomName("put", 5), selection.hitBlock);
            m_particleSystem.emit(selection.hitBlock, brokenBlock);
            resetBlockBreakSession();
            return;
        }

        resetBlockBreakSession();

        if (action == GameplayBlockAction::Place) {
            m_world.setBlock(selection.placeBlock.x, selection.placeBlock.y, selection.placeBlock.z,
                             m_player.getInventory().getSelectedBlock());
            m_dropSystem.onBlockPlaced(selection.placeBlock, m_world);
            m_placeCooldownRemaining = m_modeRules.placeCooldownSeconds();
            m_audioEngine.playClip(gameplay_state_detail::getRandomName("put", 5), selection.placeBlock);
        }
    }

    static constexpr float kPickDistance = 6.0f;

    GameStateMachine& m_fsm;
    Player& m_player;
    InputContextManager& m_context;
    InputManager& m_input;
    UIRenderer& m_uiRenderer;
    std::string& m_lastSubmittedCommand;
    physics::PhysicsSystem& m_physicsSystem;
    World& m_world;
    AudioEngine& m_audioEngine;
    ParticleSystem& m_particleSystem;
    DropSystem& m_dropSystem;
    const IGameplayModeRules& m_modeRules;
    float m_placeCooldownRemaining = 0.0f;
    BlockBreakSession m_blockBreakSession;

    float m_footstepTimer = 0.0f;
    int m_footstepIndex = 0;
};

#endif //MECRAFT_GAMEPLAYSTATE_H

