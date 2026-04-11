#ifndef MECRAFT_COMMANDSTATE_H
#define MECRAFT_COMMANDSTATE_H

#include <string>
#include <sstream>
#include <vector>

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "../../ui/KeyboardInputBox.h"
#include "../../ui/UIRenderer.h"
#include "../../player/Player.h"
#include "../../world/World.h"
#include "../../world/Block.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../audio/AudioEngine.h"
namespace physics {
class PhysicsSystem;
}


class CommandState : public IGameState {
public:
    CommandState(GameStateMachine& fsm,
                 InputContextManager& context,
                 InputManager& input,
                 UIRenderer& uiRenderer,
                 std::string& lastSubmittedCommand,
                 Player& player,
                 physics::PhysicsSystem& physicsSystem,
                 World& world,
                 AudioEngine& audioEngine,
                 ParticleSystem& particleSystem,
                 DropSystem& dropSystem)
        : m_fsm(fsm),
          m_context(context),
          m_input(input),
          m_uiRenderer(uiRenderer),
          m_lastSubmittedCommand(lastSubmittedCommand),
          m_player(player),
          m_physicsSystem(physicsSystem),
          m_world(world),
          m_audioEngine(audioEngine),
          m_particleSystem(particleSystem),
          m_dropSystem(dropSystem),
          m_inputBox(128) {
    }

    void onEnter() override {
        m_context.pushContext(InputContextType::UI);
        m_input.captureMouse(false);
        m_inputBox.open("/");
    }

    void onExit() override {
        m_context.popContext();
        if (m_context.getCurrentContext() == InputContextType::Gameplay) {
            m_input.captureMouse(true);
        }
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        const std::string textBeforeUpdate = m_inputBox.getText();
        m_inputBox.update(snapshot, dt, &m_context);

        bool appliedHistoryNavigation = false;
        if (m_inputBox.consumeHistoryPrev()) {
            recallPreviousCommand();
            appliedHistoryNavigation = true;
        } else if (m_inputBox.consumeHistoryNext()) {
            recallNextCommand();
            appliedHistoryNavigation = true;
        }

        std::string submitted;
        if (m_inputBox.consumeSubmit(&submitted)) {
            commitCommandToHistory(submitted);
            m_lastSubmittedCommand = submitted;
            m_uiRenderer.appendCommandLine(submitted);
            const bool handledTransition = executeCommand(submitted);
            if (!handledTransition) {
                m_fsm.popState();
            }
            return;
        }

        if (!appliedHistoryNavigation && m_historyCursor >= 0 && m_inputBox.getText() != textBeforeUpdate) {
            m_historyCursor = -1;
            m_historyDraft.clear();
        }

        if (m_inputBox.consumeCancel()) {
            m_fsm.popState();
            return;
        }
    }

    void render() override {
        if (!m_inputBox.isOpen()) {
            return;
        }
        m_uiRenderer.renderCommandInputBox(m_inputBox.getText());
    }

private:
    bool executeCommand(const std::string& command) {
        // 指令格式: /一级指令 二级指令
        if (command.empty() || command[0] != '/') {
            return false;
        }

        std::istringstream iss(command.substr(1)); // 去掉 '/'
        std::string primary;
        iss >> primary;

        if (primary == "gamemode") {
            std::string secondary;
            iss >> secondary;
            if (secondary == "creative") {
                switchToCreativeMode();
                return true;
            } else if (secondary == "survival") {
                switchToSurvivalMode();
                return true;
            } else {
                m_uiRenderer.appendWarningLine("Usage: /gamemode <creative|survival>");
                return false; // Command recognized but invalid argument, so we consider it handled.
            }
        }

        return false;
    }

    void switchToCreativeMode();

    void switchToSurvivalMode();

    void commitCommandToHistory(const std::string& command) {
        auto& history = commandHistoryStore();
        if (command.empty()) {
            m_historyCursor = -1;
            m_historyDraft.clear();
            return;
        }

        if (history.empty() || history.back() != command) {
            history.push_back(command);
            if (history.size() > kMaxHistoryEntries) {
                history.erase(history.begin());
            }
        }

        m_historyCursor = -1;
        m_historyDraft.clear();
    }

    void recallPreviousCommand() {
        auto& history = commandHistoryStore();
        if (history.empty()) {
            return;
        }

        if (m_historyCursor < 0) {
            m_historyDraft = m_inputBox.getText();
            m_historyCursor = static_cast<int>(history.size()) - 1;
        } else if (m_historyCursor > 0) {
            --m_historyCursor;
        }

        m_inputBox.setText(history[static_cast<size_t>(m_historyCursor)]);
    }

    void recallNextCommand() {
        auto& history = commandHistoryStore();
        if (m_historyCursor < 0) {
            return;
        }

        const int lastIndex = static_cast<int>(history.size()) - 1;
        if (m_historyCursor < lastIndex) {
            ++m_historyCursor;
            m_inputBox.setText(history[static_cast<size_t>(m_historyCursor)]);
            return;
        }

        m_historyCursor = -1;
        m_inputBox.setText(m_historyDraft);
        m_historyDraft.clear();
    }

    static std::vector<std::string>& commandHistoryStore() {
        static std::vector<std::string> s_history;
        return s_history;
    }

    GameStateMachine& m_fsm;
    InputContextManager& m_context;
    InputManager& m_input;
    UIRenderer& m_uiRenderer;
    std::string& m_lastSubmittedCommand;
    Player& m_player;
    physics::PhysicsSystem& m_physicsSystem;
    World& m_world;
    AudioEngine& m_audioEngine;
    ParticleSystem& m_particleSystem;
    DropSystem& m_dropSystem;
    KeyboardInputBox m_inputBox;
    std::string m_historyDraft;
    int m_historyCursor = -1;

    static constexpr size_t kMaxHistoryEntries = 50;
};

#endif // MECRAFT_COMMANDSTATE_H

